/* (c) YClient. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_CLIENT_POINTS_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_CLIENT_POINTS_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class IHttp;
class CHttpRequest;

class CScoreboardClientPoints
{
public:
	CScoreboardClientPoints(IHttp *pHttp);
	~CScoreboardClientPoints();

	// 根据玩家名获取分数，如果没有缓存则自动请求
	int GetPointsForPlayer(const char *pPlayerName);

	// 主动查询多个玩家的分数（后台查询，不阻塞）
	void QueryPlayersPoints(const char **ppPlayerNames, int PlayerCount);

	// 检查是否正在获取某玩家的数据
	bool IsFetching(const char *pPlayerName) const;

	// 清空所有数据
	void Clear();

private:
	class CPlayerPointsRequest : public std::enable_shared_from_this<CPlayerPointsRequest>
	{
	public:
		CPlayerPointsRequest(IHttp *pHttp, const char *pPlayerName);
		~CPlayerPointsRequest();

		void Fetch();
		bool IsDone() const;
		int GetTotalPoints();
		const char *GetPlayerName() const { return m_aPlayerName; }

	private:
		std::shared_ptr<CHttpRequest> m_pHttpRequest;
		IHttp *m_pHttp;
		char m_aPlayerName[64];
		int m_TotalPoints;
		bool m_bParsed;

		void ParseJson();

		friend class CScoreboardClientPoints;
	};

	IHttp *m_pHttp;
	std::unordered_map<std::string, std::shared_ptr<CPlayerPointsRequest>> m_RequestMap;
	std::unordered_map<std::string, int> m_CachedPoints;
};

#endif
