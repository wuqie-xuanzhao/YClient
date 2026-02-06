/* (c) YClient. See licence.txt in the root of the distribution for more information. */
#include "scoreboard_client_points.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/http.h>

#include <cstring>

// URL编码函数，将玩家名称编码以支持特殊字符
static void UrlEncode(const char *pString, char *pOut, size_t OutSize)
{
	if(!pString || !pOut || OutSize == 0)
		return;

	size_t i = 0;
	size_t j = 0;
	size_t StringLen = str_length(pString);

	while(i < StringLen && j < OutSize - 1)
	{
		unsigned char c = (unsigned char)pString[i];

		// 保留不需要编码的字符：字母、数字、-、_、.、~
		if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
		{
			pOut[j++] = c;
		}
		else
		{
			// 其他字符进行百分号编码
			int n = str_format(pOut + j, OutSize - j, "%%%02X", c);
			if(n > 0)
				j += n;
			else
				break;
		}
		i++;
	}
	pOut[j] = '\0';
}

CScoreboardClientPoints::CPlayerPointsRequest::CPlayerPointsRequest(IHttp *pHttp, const char *pPlayerName) :
	m_pHttp(pHttp),
	m_TotalPoints(0),
	m_bParsed(false)
{
	str_copy(m_aPlayerName, pPlayerName, sizeof(m_aPlayerName));

	// 对玩家名称进行URL编码
	char aEncodedName[512];
	UrlEncode(pPlayerName, aEncodedName, sizeof(aEncodedName));

	// 构建 URL: https://ddnet.org/players/?json2=<encodedPlayerName>
	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);

	m_pHttpRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pHttpRequest->WriteToMemory();
	m_pHttpRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	m_pHttpRequest->MaxResponseSize(2 * 1024 * 1024); // 2MB max
	m_pHttpRequest->FailOnErrorStatus(false); // 玩家不存在也不算错误

	dbg_msg("scoreboard_points", "created request for player: %s, url: %s", pPlayerName, aUrl);
}

CScoreboardClientPoints::CPlayerPointsRequest::~CPlayerPointsRequest()
{
	if(m_pHttpRequest && !m_pHttpRequest->Done())
	{
		m_pHttpRequest->Abort();
	}
}

void CScoreboardClientPoints::CPlayerPointsRequest::Fetch()
{
	if(m_pHttpRequest && m_pHttp)
	{
		m_pHttp->Run(m_pHttpRequest);
	}
}

bool CScoreboardClientPoints::CPlayerPointsRequest::IsDone() const
{
	if(!m_pHttpRequest)
		return false;
	return m_pHttpRequest->State() == EHttpState::DONE;
}

int CScoreboardClientPoints::CPlayerPointsRequest::GetTotalPoints()
{
	if(!m_bParsed && IsDone())
	{
		ParseJson();
		m_bParsed = true;
	}
	return m_TotalPoints;
}

void CScoreboardClientPoints::CPlayerPointsRequest::ParseJson()
{
	if(!m_pHttpRequest)
		return;

	// 检查HTTP状态码，如果不是成功响应则跳过
	int StatusCode = m_pHttpRequest->StatusCode();
	if(StatusCode < 200 || StatusCode >= 300)
	{
		// 玩家不存在或请求失败
		dbg_msg("scoreboard_points", "player %s: HTTP %d", m_aPlayerName, StatusCode);
		return;
	}

	json_value *pJson = m_pHttpRequest->ResultJson();
	if(!pJson)
	{
		// JSON解析失败，可能玩家不存在或响应为空
		dbg_msg("scoreboard_points", "player %s: JSON parse failed", m_aPlayerName);
		return;
	}

	// JSON 格式来自 https://ddnet.org/players/?json2=<name>
	// 格式为：
	// {
	//   "player": "DYL",
	//   "points": {
	//     "rank": 1260,
	//     "points": 13333,    <-- 这是玩家的总分
	//     "total": 33297
	//   }
	// }

	if(pJson->type == json_type::json_object)
	{
		// 查找 "points" 对象
		json_value *pPointsData = nullptr;
		for(unsigned int i = 0; i < pJson->u.object.length; i++)
		{
			if(str_comp(pJson->u.object.values[i].name, "points") == 0)
			{
				pPointsData = pJson->u.object.values[i].value;
				break;
			}
		}

		if(!pPointsData)
		{
			// 没有找到 "points" 对象，玩家从未完成过关卡，分数为 0
			m_TotalPoints = 0;
			dbg_msg("scoreboard_points", "player %s: no 'points' object, set to 0", m_aPlayerName);
			json_value_free(pJson);
			return;
		}

		if(pPointsData->type == json_type::json_object)
		{
			// 在 "points" 对象中查找 "points" 字段
			for(unsigned int i = 0; i < pPointsData->u.object.length; i++)
			{
				if(str_comp(pPointsData->u.object.values[i].name, "points") == 0)
				{
					json_value *pValue = pPointsData->u.object.values[i].value;
					if(pValue->type == json_type::json_integer)
					{
						m_TotalPoints = (int)pValue->u.integer;
						dbg_msg("scoreboard_points", "player %s total points: %d", m_aPlayerName, m_TotalPoints);
					}
					break;
				}
			}
		}
		else
		{
			// "points" 不是对象类型
			dbg_msg("scoreboard_points", "player %s: 'points' is not an object", m_aPlayerName);
		}
	}

	json_value_free(pJson);
}

CScoreboardClientPoints::CScoreboardClientPoints(IHttp *pHttp) :
	m_pHttp(pHttp)
{
}

CScoreboardClientPoints::~CScoreboardClientPoints()
{
	Clear();
}

int CScoreboardClientPoints::GetPointsForPlayer(const char *pPlayerName)
{
	if(!m_pHttp || !pPlayerName || !pPlayerName[0])
		return 0;

	// 首先检查缓存
	auto it = m_CachedPoints.find(pPlayerName);
	if(it != m_CachedPoints.end())
	{
		return it->second;
	}

	// 检查是否已经有请求在进行或已完成
	auto reqIt = m_RequestMap.find(pPlayerName);
	if(reqIt != m_RequestMap.end())
	{
		auto pRequest = reqIt->second;
		if(pRequest->IsDone())
		{
			int points = pRequest->GetTotalPoints();
			// 将结果缓存（包括 0 分）
			m_CachedPoints[pPlayerName] = points;
			// 清除已完成的请求，节省内存
			m_RequestMap.erase(pPlayerName);
			return points;
		}
		// 请求还在进行中
		return 0;
	}

	// 创建新请求
	auto pNewRequest = std::make_shared<CPlayerPointsRequest>(m_pHttp, pPlayerName);
	if(pNewRequest)
	{
		m_RequestMap[pPlayerName] = pNewRequest;
		pNewRequest->Fetch();
		dbg_msg("scoreboard_points", "fetching points for: %s", pPlayerName);
		return 0;
	}

	return 0;
}

void CScoreboardClientPoints::QueryPlayersPoints(const char **ppPlayerNames, int PlayerCount)
{
	if(!m_pHttp || !ppPlayerNames || PlayerCount <= 0)
		return;

	// 触发所有玩家的查询（如果还没有缓存的话）
	for(int i = 0; i < PlayerCount; i++)
	{
		if(ppPlayerNames[i] && ppPlayerNames[i][0])
		{
			// 如果还没有缓存和请求，则创建请求
			auto cachedIt = m_CachedPoints.find(ppPlayerNames[i]);
			auto reqIt = m_RequestMap.find(ppPlayerNames[i]);

			if(cachedIt == m_CachedPoints.end() && reqIt == m_RequestMap.end())
			{
				auto pNewRequest = std::make_shared<CPlayerPointsRequest>(m_pHttp, ppPlayerNames[i]);
				if(pNewRequest)
				{
					m_RequestMap[ppPlayerNames[i]] = pNewRequest;
					pNewRequest->Fetch();
					dbg_msg("scoreboard_points", "background fetching points for: %s", ppPlayerNames[i]);
				}
			}
		}
	}
}

bool CScoreboardClientPoints::IsFetching(const char *pPlayerName) const
{
	auto it = m_RequestMap.find(pPlayerName);
	if(it != m_RequestMap.end())
	{
		return !it->second->IsDone();
	}
	return false;
}

void CScoreboardClientPoints::Clear()
{
	m_CachedPoints.clear();
	for(auto &entry : m_RequestMap)
	{
		if(entry.second && !entry.second->IsDone())
		{
			if(entry.second->m_pHttpRequest)
			{
				entry.second->m_pHttpRequest->Abort();
			}
		}
	}
	m_RequestMap.clear();
}
