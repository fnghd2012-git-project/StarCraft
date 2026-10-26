#include "PlayClient.h"

namespace
{
void Check(bool ok, const string& what)
{
    if (!ok)
    {
        throw runtime_error("Ranking check: " + what);
    }
}
template <class Predicate> void Wait(Predicate ready, const string& what)
{
    const auto until = chrono::steady_clock::now() + chrono::seconds(20);
    while (!ready())
    {
        if (chrono::steady_clock::now() > until)
        {
            throw runtime_error("Ranking timeout: " + what);
        }
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}
struct stActor
{
    string server, name;
    wire::UnitState unit;
    unique_ptr<CClientConnection> up_world;
};
void Enter(stActor& actor, const fs::path& path)
{
    CClientConnection game(LoadServerConfig(path, actor.server), true);
    wire::JoinGameRequest login;
    login.set_name(actor.name);
    login.set_password("PortfolioTest!2026");
    actor.unit = RequestMessage<wire::JoinGameReply>(
                     game, Protocol::Type::CM_JoinGame, login, Protocol::Type::SM_JoinGameReply)
                     .unit();
    auto ticket = RequestMessage<wire::WorldTicket>(
        game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
    wire::EnterWorldRequest request;
    request.set_claims(ticket.claims());
    request.set_signature(ticket.signature());
    actor.up_world = make_unique<CClientConnection>(LoadServerConfig(path, "World"), true);
    const auto reply = RequestMessage<wire::WorldReply>(
        *actor.up_world, Protocol::Type::CM_EnterWorld, request, Protocol::Type::SM_WorldReply);
    Check(reply.ok(), "World admission: " + reply.message());
    actor.unit = reply.unit();
}
void Returned(stActor& actor)
{
    Wait(
        [&]
        {
            return !actor.up_world->ReturnTicket().claims().empty();
        },
        "return ticket");
}
void Reenter(stActor& actor, const fs::path& path)
{
    Returned(actor);
    const auto ticket = actor.up_world->ReturnTicket();
    CClientConnection game(LoadServerConfig(path, actor.server), true);
    wire::EnterWorldRequest resume;
    resume.set_claims(ticket.claims());
    resume.set_signature(ticket.signature());
    const auto reply = RequestMessage<wire::JoinGameReply>(
        game, Protocol::Type::CM_ResumeGame, resume, Protocol::Type::SM_JoinGameReply);
    if (reply.retry_after_seconds())
    {
        bool denied = false;
        try
        {
            RequestMessage<wire::WorldTicket>(
                game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
        }
        catch (const exception&)
        {
            denied = true;
        }
        Check(denied, "cooldown rejects premature entry");
        this_thread::sleep_for(chrono::seconds(reply.retry_after_seconds() + 1));
    }
    else
    {
        // DB/티켓은 정수 초, World의 로컬 대기는 steady_clock이다. 초 경계의 잔여 1초를 기다린다.
        this_thread::sleep_for(chrono::seconds(1));
    }
    const auto next = RequestMessage<wire::WorldTicket>(
        game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
    wire::EnterWorldRequest enter;
    enter.set_claims(next.claims());
    enter.set_signature(next.signature());
    actor.up_world = make_unique<CClientConnection>(LoadServerConfig(path, "World"), true);
    const auto entered = RequestMessage<wire::WorldReply>(
        *actor.up_world, Protocol::Type::CM_EnterWorld, enter, Protocol::Type::SM_WorldReply);
    Check(entered.ok(), "World reentry: " + entered.message());
    actor.unit = entered.unit();
}
void Back(stActor& actor)
{
    Check(RequestMessage<wire::ActionReply>(*actor.up_world,
                                            Protocol::Type::CM_LeaveWorld,
                                            wire::EmptyRequest{},
                                            Protocol::Type::SM_ActionReply)
              .ok(),
          "back");
    Returned(actor);
}
void Attack(stActor& actor, const string& id)
{
    wire::AttackRequest request;
    request.set_target_id(id);
    Check(RequestMessage<wire::ActionReply>(
              *actor.up_world, Protocol::Type::CM_Attack, request, Protocol::Type::SM_ActionReply)
              .ok(),
          "attack " + id);
}
void Place(stActor& actor, double x, double y)
{
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    Check(RequestMessage<wire::ActionReply>(
              *actor.up_world, Protocol::Type::CM_Move, request, Protocol::Type::SM_ActionReply)
              .ok(),
          "move");
    Wait(
        [&]
        {
            const auto state = actor.up_world->Snapshot();
            for (const auto& unit : state.units())
            {
                if (unit.player_id() == actor.unit.player_id() && abs(unit.x() - x) < .1 &&
                    abs(unit.y() - y) < .1)
                {
                    return true;
                }
            }
            return false;
        },
        "move completion");
}
wire::RankReply Rank(stActor& actor, wire::RankMetric metric = wire::RANK_KILLS)
{
    this_thread::sleep_for(chrono::milliseconds(1100));
    wire::RankRequest request;
    request.set_metric(metric);
    return RequestMessage<wire::RankReply>(
        *actor.up_world, Protocol::Type::CM_Rank, request, Protocol::Type::SM_RankReply);
}
wire::RankEntry Row(const wire::RankReply& reply, const stActor& actor)
{
    for (const auto& row : reply.top())
    {
        if (row.player_id() == actor.unit.player_id())
        {
            return row;
        }
    }
    throw runtime_error("Ranking member missing: " + actor.name);
}
void Signal(const fs::path& directory, const string& name)
{
    ofstream(directory / name) << "ready";
}
} // namespace

void RunRankingCheck(const fs::path& path)
{
    const auto directory = path.parent_path();
    stActor terran{"C", "RankMarine"}, zerg{"A", "RankZerg"}, caster{"B", "RankCaster"},
        ally{"B", "RankAlly"};
    Enter(terran, path);
    Enter(zerg, path);
    Enter(caster, path);
    Enter(ally, path);
    auto initial = Rank(terran);
    Check(initial.ok() && initial.top_size() == 4 && initial.self().entries() == 1 &&
              initial.self().kills() == 0,
          "first entry and zero scores");
    for (int i = 1; i < initial.top_size(); ++i)
    {
        Check(initial.top(i - 1).player_id() > initial.top(i).player_id(), "tie order");
    }
    wire::RankRequest invalid;
    invalid.set_metric(wire::RANK_UNSPECIFIED);
    Check(!RequestMessage<wire::RankReply>(
               *terran.up_world, Protocol::Type::CM_Rank, invalid, Protocol::Type::SM_RankReply)
               .ok(),
          "invalid metric rejected");
    Attack(terran, zerg.unit.player_id());
    Returned(zerg);
    Reenter(zerg, path);
    Back(zerg);
    Reenter(zerg, path);
    auto basic = Rank(terran);
    Check(basic.ok() && basic.self().kills() == 1 && Row(basic, zerg).deaths() == 1 &&
              Row(basic, zerg).entries() == 3,
          "kill/death/reentry; back is not death");
    Place(caster, 70, 70);
    Place(zerg, 104, 60);
    wire::SkillRequest storm;
    storm.set_slot(1);
    storm.set_x(104);
    storm.set_y(60);
    Check(RequestMessage<wire::ActionReply>(
              *caster.up_world, Protocol::Type::CM_Skill, storm, Protocol::Type::SM_ActionReply)
              .ok(),
          "storm");
    Back(caster);
    Returned(zerg);
    Returned(ally);
    const auto skills = Rank(terran, wire::RANK_DEATHS);
    Check(skills.ok() && Row(skills, caster).kills() == 1 && Row(skills, caster).deaths() == 0 &&
              Row(skills, zerg).deaths() == 2 && Row(skills, ally).deaths() == 1,
          "departed caster kill; friendly death without kill");
    cout << "[OK] ranking combat, friendly fire, voluntary return, reentry, ties; match="
         << initial.match_id() << endl;
    Reenter(zerg, path);
    Signal(directory, "outage.ready");
    Wait(
        [&]
        {
            return fs::exists(directory / "outage.down");
        },
        "Redis stop");
    const auto before = terran.up_world->Snapshot().tick();
    Attack(terran, zerg.unit.player_id());
    Returned(zerg);
    Place(terran, 59, 104);
    Check(!Rank(terran).ok() && terran.up_world->Snapshot().tick() > before,
          "Redis outage does not stop combat");
    Signal(directory, "outage.checked");
    Wait(
        [&]
        {
            return fs::exists(directory / "outage.restored");
        },
        "Redis restart");
    wire::RankReply restored;
    Wait(
        [&]
        {
            restored = Rank(terran);
            return restored.ok();
        },
        "ranking restored");
    Check(restored.self().kills() == 2 && Row(restored, zerg).deaths() == 3 &&
              Row(restored, zerg).entries() == 4 && Row(restored, caster).kills() == 1,
          "full restore includes departed players, no duplicate increments");
    cout << "[OK] Redis empty restart restored totals; combat continued during outage" << endl;
    Attack(terran, "building:Zerg");
    this_thread::sleep_for(chrono::milliseconds(200));
    Attack(terran, "building:Protoss");
    Returned(terran);
    this_thread::sleep_for(chrono::seconds(4));
    Reenter(terran, path);
    const auto next = Rank(terran, wire::RANK_ENTRIES);
    Check(next.ok() && next.match_id() != initial.match_id() && next.self().kills() == 0 &&
              next.self().deaths() == 0 && next.self().entries() == 1,
          "new match resets scores");
    cout << "[OK] new match reset; previous=" << initial.match_id() << " current=" << next.match_id() << endl;
    Check(
        RequestMessage<wire::ActionReply>(
            *terran.up_world, Protocol::Type::CM_Logout, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply)
            .ok(),
        "logout");
}
