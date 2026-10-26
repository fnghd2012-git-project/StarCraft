#pragma once

#include "ClientConnection.h"
#include "BattleDecision.h"

// 더미는 목표와 요청만 결정한다. 실제 이동 속도, 공격 사거리와 피해량은 서버가 판정한다.
// Play의 전환 잠금 안에서만 사용하므로 별도 스레드나 잠금을 소유하지 않는다.
class CAutoBattle final
{
public:
    void Start(int seconds = 0, bool buildingsOnly = false);
    void Stop()
    {
        m_active = false;
    }
    bool Active() const
    {
        return m_active;
    }
    void Update(CClientConnection& world, const string& playerId);

private:
    bool m_active = false;
    string m_targetId;
    CBattleDecision m_decision;
    chrono::steady_clock::time_point m_until;
    chrono::steady_clock::time_point m_nextUpdate;
};
