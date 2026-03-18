#ifndef GAME_SERVER_SCORE_POINTS_H
#define GAME_SERVER_SCORE_POINTS_H

#include <memory>
#include <string>
#include <unordered_map>

#include <engine/http.h>
#include <engine/shared/http.h>

class IHttp;

class CScorePoints
{
public:
	CScorePoints(IHttp *pHttp);
	~CScorePoints();

	int GetPointsForPlayer(const char *pPlayerName);
	bool IsFetching(const char *pPlayerName) const;
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

		friend class CScorePoints;
	};

	IHttp *m_pHttp;
	std::unordered_map<std::string, std::shared_ptr<CPlayerPointsRequest>> m_RequestMap;
	std::unordered_map<std::string, int> m_CachedPoints;
};

#endif