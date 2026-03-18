#include "score_points.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/http.h>

#include <cstring>

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

		if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
		{
			pOut[j++] = c;
		}
		else
		{
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

CScorePoints::CPlayerPointsRequest::CPlayerPointsRequest(IHttp *pHttp, const char *pPlayerName) :
	m_pHttp(pHttp),
	m_TotalPoints(0),
	m_bParsed(false)
{
	str_copy(m_aPlayerName, pPlayerName, sizeof(m_aPlayerName));

	char aEncodedName[512];
	UrlEncode(pPlayerName, aEncodedName, sizeof(aEncodedName));

	char aUrl[1024];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEncodedName);

	m_pHttpRequest = std::make_shared<CHttpRequest>(aUrl);
	m_pHttpRequest->WriteToMemory();
	m_pHttpRequest->Timeout(CTimeout{5000, 10000, 0, 0});
	m_pHttpRequest->MaxResponseSize(2 * 1024 * 1024);
	m_pHttpRequest->FailOnErrorStatus(false);

	dbg_msg("score_points", "created request for player: %s, url: %s", pPlayerName, aUrl);
}

CScorePoints::CPlayerPointsRequest::~CPlayerPointsRequest()
{
	if(m_pHttpRequest && !m_pHttpRequest->Done())
	{
		m_pHttpRequest->Abort();
	}
}

void CScorePoints::CPlayerPointsRequest::Fetch()
{
	if(m_pHttpRequest && m_pHttp)
	{
		m_pHttp->Run(m_pHttpRequest);
	}
}

bool CScorePoints::CPlayerPointsRequest::IsDone() const
{
	if(!m_pHttpRequest)
		return false;
	return m_pHttpRequest->State() == EHttpState::DONE;
}

int CScorePoints::CPlayerPointsRequest::GetTotalPoints()
{
	if(!m_bParsed && IsDone())
	{
		ParseJson();
		m_bParsed = true;
	}
	return m_TotalPoints;
}

void CScorePoints::CPlayerPointsRequest::ParseJson()
{
	if(!m_pHttpRequest)
		return;

	int StatusCode = m_pHttpRequest->StatusCode();
	if(StatusCode < 200 || StatusCode >= 300)
	{
		dbg_msg("score_points", "player %s: HTTP %d", m_aPlayerName, StatusCode);
		return;
	}

	json_value *pJson = m_pHttpRequest->ResultJson();
	if(!pJson)
	{
		dbg_msg("score_points", "player %s: JSON parse failed", m_aPlayerName);
		return;
	}

	if(pJson->type == json_type::json_object)
	{
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
			m_TotalPoints = 0;
			dbg_msg("score_points", "player %s: no 'points' object, set to 0", m_aPlayerName);
			json_value_free(pJson);
			return;
		}

		if(pPointsData->type == json_type::json_object)
		{
			for(unsigned int i = 0; i < pPointsData->u.object.length; i++)
			{
				if(str_comp(pPointsData->u.object.values[i].name, "points") == 0)
				{
					json_value *pValue = pPointsData->u.object.values[i].value;
					if(pValue->type == json_type::json_integer)
					{
						m_TotalPoints = (int)pValue->u.integer;
						dbg_msg("score_points", "player %s total points: %d", m_aPlayerName, m_TotalPoints);
					}
					break;
				}
			}
		}
	}

	json_value_free(pJson);
}

CScorePoints::CScorePoints(IHttp *pHttp) :
	m_pHttp(pHttp)
{
}

CScorePoints::~CScorePoints()
{
	Clear();
}

int CScorePoints::GetPointsForPlayer(const char *pPlayerName)
{
	if(!m_pHttp || !pPlayerName || !pPlayerName[0])
		return 0;

	auto it = m_CachedPoints.find(pPlayerName);
	if(it != m_CachedPoints.end())
	{
		return it->second;
	}

	auto reqIt = m_RequestMap.find(pPlayerName);
	if(reqIt != m_RequestMap.end())
	{
		auto pRequest = reqIt->second;
		if(pRequest->IsDone())
		{
			int points = pRequest->GetTotalPoints();
			m_CachedPoints[pPlayerName] = points;
			m_RequestMap.erase(pPlayerName);
			return points;
		}
		return 0;
	}

	auto pNewRequest = std::make_shared<CPlayerPointsRequest>(m_pHttp, pPlayerName);
	if(pNewRequest)
	{
		m_RequestMap[pPlayerName] = pNewRequest;
		pNewRequest->Fetch();
		dbg_msg("score_points", "fetching points for: %s", pPlayerName);
		return 0;
	}

	return 0;
}

bool CScorePoints::IsFetching(const char *pPlayerName) const
{
	auto it = m_RequestMap.find(pPlayerName);
	if(it != m_RequestMap.end())
	{
		return !it->second->IsDone();
	}
	return false;
}

void CScorePoints::Clear()
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