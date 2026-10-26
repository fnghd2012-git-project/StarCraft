#pragma once
#include "Packet.h"

struct stBattleOrder
{
    Protocol::Type type = Protocol::Type::CM_Move;
    string payload;
    string target;
};

// 대화형 더미와 다중 접속 더미가 같은 판단을 사용한다. 최종 판정은 항상 서버에 요청한다.
class CBattleDecision
{
public:
    void Reset(bool rally = false, bool buildingsOnly = false)
    {
        m_target.clear();
        m_rally = rally;
        m_moving = false;
        m_buildingsOnly = buildingsOnly;
    }
    stBattleOrder Decide(const wire::WorldState& state,
                         const wire::MapInfo& map,
                         const vector<wire::SkillInfo>& skills,
                         const string& playerId);

private:
    string m_target;
    bool m_rally = false;
    bool m_moving = false;
    bool m_buildingsOnly = false;
};
