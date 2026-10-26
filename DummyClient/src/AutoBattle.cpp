#include "AutoBattle.h"
#include "PlayClient.h"

void CAutoBattle::Start(int seconds, bool buildingsOnly)
{
    m_active = true;
    m_targetId.clear();
    m_decision.Reset(false, buildingsOnly);
    m_nextUpdate = chrono::steady_clock::now();
    m_until = seconds > 0 ? m_nextUpdate + chrono::seconds(seconds) : chrono::steady_clock::time_point::max();
    PrintPlayLine(buildingsOnly
                      ? "[AUTO] 건물 공략 전용 시연 / auto off로 중지"
                      : "[AUTO] 자동 전투 시작: 주변 적 유닛 우선, 적이 없으면 건물 공략 / auto off로 중지");
}

void CAutoBattle::Update(CClientConnection& world, const string& playerId)
{
    const auto now = chrono::steady_clock::now();
    if (!m_active || now < m_nextUpdate)
    {
        return;
    }
    m_nextUpdate = now + chrono::milliseconds(250);
    if (!world.InWorld() || now >= m_until)
    {
        Stop();
        return;
    }
    const auto state = world.Snapshot();
    if (!state.complete())
    {
        return;
    }
    const auto order = m_decision.Decide(state, world.Map(), world.Skills(), playerId);
    if (order.payload.empty())
    {
        return;
    }
    if (!order.target.empty() && m_targetId != order.target)
    {
        m_targetId = order.target;
        PrintPlayLine("[AUTO] 목표: " + m_targetId);
    }
    wire::ActionReply reply;
    if (order.type == Protocol::Type::CM_Attack)
    {
        wire::AttackRequest request;
        request.ParseFromString(order.payload);
        reply = RequestMessage<wire::ActionReply>(world, order.type, request, Protocol::Type::SM_ActionReply);
    }
    else if (order.type == Protocol::Type::CM_Skill)
    {
        wire::SkillRequest request;
        request.ParseFromString(order.payload);
        reply = RequestMessage<wire::ActionReply>(world, order.type, request, Protocol::Type::SM_ActionReply);
    }
    else
    {
        wire::MoveRequest request;
        request.ParseFromString(order.payload);
        reply = RequestMessage<wire::ActionReply>(world, order.type, request, Protocol::Type::SM_ActionReply);
    }
    // 사거리/쿨다운/상태는 최신 서버 상태가 기준이다. 정상 거부 뒤 다음 스냅샷으로 다시 판단한다.
    if (!reply.ok() && !world.InWorld())
    {
        Stop();
    }
}
