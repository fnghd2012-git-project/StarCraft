#include "PlayClient.h"

namespace
{
constexpr char TestPassword[] = "PortfolioTest!2026";
void Check(bool ok, const string& message)
{
    if (!ok)
    {
        throw runtime_error("Account/skill check: " + message);
    }
}
template <class Predicate> void Wait(Predicate ready, const string& message)
{
    const auto until = chrono::steady_clock::now() + chrono::seconds(8);
    while (!ready())
    {
        if (chrono::steady_clock::now() >= until)
        {
            throw runtime_error("Timeout: " + message);
        }
        this_thread::sleep_for(chrono::milliseconds(20));
    }
}
wire::JoinGameReply Login(CClientConnection& game,
                          const string& account,
                          const string& password = TestPassword)
{
    wire::JoinGameRequest request;
    request.set_name(account);
    request.set_password(password);
    return RequestMessage<wire::JoinGameReply>(
        game, Protocol::Type::CM_JoinGame, request, Protocol::Type::SM_JoinGameReply);
}
void Logout(CClientConnection& connection)
{
    Check(RequestMessage<wire::ActionReply>(
              connection, Protocol::Type::CM_Logout, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply)
              .ok(),
          "logout commit");
}
wire::WorldReply Enter(CClientConnection& game, CClientConnection& world)
{
    const auto ticket = RequestMessage<wire::WorldTicket>(
        game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
    wire::EnterWorldRequest request;
    request.set_claims(ticket.claims());
    request.set_signature(ticket.signature());
    return RequestMessage<wire::WorldReply>(
        world, Protocol::Type::CM_EnterWorld, request, Protocol::Type::SM_WorldReply);
}
wire::UnitState Find(CClientConnection& world, const string& id)
{
    const auto state = world.Snapshot();
    for (const auto& unit : state.units())
    {
        if (unit.player_id() == id)
        {
            return unit;
        }
    }
    return {};
}
struct stActor
{
    unique_ptr<CClientConnection> up_world;
    wire::UnitState unit;
    wire::UnitState State()
    {
        return Find(*up_world, unit.player_id());
    }
};
wire::ActionReply MoveTo(stActor& actor, double x, double y)
{
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    return RequestMessage<wire::ActionReply>(
        *actor.up_world, Protocol::Type::CM_Move, request, Protocol::Type::SM_ActionReply);
}
void Place(stActor& actor, double x, double y)
{
    Check(MoveTo(actor, x, y).ok(), "move actor");
    Wait(
        [&]
        {
            const auto unit = actor.State();
            return unit.hp() > 0 && abs(unit.x() - x) < 0.1 && abs(unit.y() - y) < 0.1;
        },
        "actor reaches test position");
}
wire::ActionReply Attack(stActor& source, stActor& target)
{
    wire::AttackRequest request;
    request.set_target_id(target.unit.player_id());
    return RequestMessage<wire::ActionReply>(
        *source.up_world, Protocol::Type::CM_Attack, request, Protocol::Type::SM_ActionReply);
}
wire::ActionReply Cast(stActor& actor, int slot, const string& target = "", double x = 0, double y = 0)
{
    wire::SkillRequest request;
    request.set_slot(slot);
    request.set_target_id(target);
    request.set_x(x);
    request.set_y(y);
    return RequestMessage<wire::ActionReply>(
        *actor.up_world, Protocol::Type::CM_Skill, request, Protocol::Type::SM_ActionReply);
}
} // namespace

void RunAccountCheck(const fs::path& configPath)
{
    const auto configA = LoadServerConfig(configPath, "A"), configB = LoadServerConfig(configPath, "B");
    wire::UnitState saved;
    {
        CClientConnection game(configA, true);
        saved = Login(game, "AccountCheck").unit();
        CClientConnection duplicate(configB, true);
        bool denied = false;
        try
        {
            Login(duplicate, "AccountCheck");
        }
        catch (const exception& error)
        {
            denied = string(error.what()).find("already logged") != string::npos;
        }
        Check(denied, "duplicate account across game servers denied");
        Logout(game);
    }
    {
        CClientConnection wrong(configA, true);
        bool denied = false;
        try
        {
            Login(wrong, "AccountCheck", "WrongPassword!");
        }
        catch (const exception& error)
        {
            denied = string(error.what()).find("password") != string::npos;
        }
        Check(denied, "wrong password rejected");
    }
    {
        CClientConnection game(configB, true);
        const auto other = Login(game, "AccountCheck").unit();
        Check(other.player_id() == saved.player_id() && other.race() != saved.race(),
              "stable account with per-server unit");
        Logout(game);
    }
    {
        CClientConnection game(configA, true);
        const auto restored = Login(game, "AccountCheck").unit();
        Check(restored.player_id() == saved.player_id() && restored.unit_index() == saved.unit_index(),
              "saved unit restored");
        CClientConnection world(LoadServerConfig(configPath, "World"), true);
        Check(Enter(game, world).ok(), "account World transfer");
        Logout(world);
    }
    {
        CClientConnection game(configB, true);
        const auto restored = Login(game, "AccountCheck");
        Check(restored.retry_after_seconds() > 0, "cooldown survives reconnect and server switch");
        bool denied = false;
        try
        {
            RequestMessage<wire::WorldTicket>(
                game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
        }
        catch (const exception& error)
        {
            denied = string(error.what()).find("cooldown") != string::npos;
        }
        Check(denied, "reentry blocked by persisted cooldown");
        Logout(game);
    }
    {
        CClientConnection first(configA, true), second(configB, true);
        array<bool, 2> accepted{};
        // 서로 다른 프로세스의 DB 쓰레드(워커)가 같은 계정을 동시에 로그인해도 한 쪽만 성공해야 한다.
        {
            jthread one(
                [&]
                {
                    try
                    {
                        Login(first, "AccountCheck");
                        accepted[0] = true;
                    }
                    catch (const exception&)
                    {
                    }
                });
            jthread two(
                [&]
                {
                    try
                    {
                        Login(second, "AccountCheck");
                        accepted[1] = true;
                    }
                    catch (const exception&)
                    {
                    }
                });
        }
        Check(accepted[0] != accepted[1], "concurrent account login must have one winner");
        Logout(accepted[0] ? first : second);
    }
    PrintPlayLine("[OK] Password authentication, duplicate login rejection, stable ID, per-server unit "
                  "persistence, reconnect cooldown");
}

void RunSkillCheck(const fs::path& configPath)
{
    // 테스트 스크립트가 별도 Realm에 각 유닛을 저장한다. 일반 로그인 경로로만 불러온다.
    const array<string, 11> names = {"Marine",
                                     "Ghost",
                                     "Medic",
                                     "Goliath",
                                     "Zealot",
                                     "HighTemplar",
                                     "Zergling",
                                     "Hydralisk",
                                     "Defiler",
                                     "HighTemplar2",
                                     "LowMPTemplar"};
    const array<int, 11> indexes = {101, 102, 103, 104, 201, 202, 301, 302, 303, 202, 203};
    array<stActor, 11> actors;
    for (size_t i = 0; i < actors.size(); ++i)
    {
        const string server = i < 4 ? "C" : (i < 6 || i >= 9) ? "B" : "A";
        CClientConnection game(LoadServerConfig(configPath, server), true);
        auto& actor = actors[i];
        actor.unit = Login(game, names[i]).unit();
        Check(actor.unit.unit_index() == static_cast<unsigned>(indexes[i]), "seeded unit loaded");
        wire::SkillRequest invalid;
        invalid.set_slot(1);
        Check(!RequestMessage<wire::ActionReply>(
                   game, Protocol::Type::CM_Skill, invalid, Protocol::Type::SM_ActionReply)
                   .ok(),
              "game server skill blocked");
        actor.up_world = make_unique<CClientConnection>(LoadServerConfig(configPath, "World"), true);
        Check(Enter(game, *actor.up_world).ok(), "skill actor World login");
        Place(actor, 50 + static_cast<double>(i), 50);
    }
    auto& marine = actors[0];
    auto& ghost = actors[1];
    auto& medic = actors[2];
    auto& goliath = actors[3];
    auto& zealot = actors[4];
    auto& templar = actors[5];
    auto& zergling = actors[6];
    auto& hydra = actors[7];
    auto& defiler = actors[8];
    Check(!Cast(zealot, 1).ok() && !Cast(marine, 6).ok(), "empty/invalid skill slots");
    Check(!Cast(templar, 1, "", -1, 50).ok() && !Cast(templar, 1, "", 120, 120).ok(), "point bounds/range");
    // 거부된 시전은 비용/영역을 만들지 않으며, 방어력과 보호막은 서로 다른 규칙을 따른다.
    Check(!Cast(actors[10], 1, "", 60, 50).ok(), "insufficient MP rejected");
    Check(!Attack(marine, ghost).ok(), "friendly basic attack rejected");
    const int armoredHP = goliath.State().hp();
    Check(Attack(hydra, goliath).ok(), "attack armored large unit");
    Wait(
        [&]
        {
            return goliath.State().hp() == armoredHP - 7;
        },
        "10 damage minus 3 armor");
    const int shieldBefore = zealot.State().shield(), hpBefore = zealot.State().hp();
    Check(Attack(ghost, zealot).ok(), "concussive attack against shields");
    Wait(
        [&]
        {
            return zealot.State().shield() == shieldBefore - 10;
        },
        "shields take full damage before size ratio");
    Check(zealot.State().hp() == hpBefore && actors[10].State().mp() <= 20,
          "shield absorbs damage; rejected skill does not underflow MP");
    PrintPlayLine("[OK] Insufficient MP/friendly-fire rejection, armor reduction and shield-first damage");
    const int initialHP = marine.State().hp();
    Check(Cast(marine, 1).ok(), "Stim cast");
    Wait(
        [&]
        {
            return marine.State().stimulated();
        },
        "Stim flag");
    Check(marine.State().hp() == initialHP - 10 && !Cast(marine, 1).ok(), "Stim cost/cooldown");
    Check(Cast(medic, 1, marine.unit.player_id()).ok(), "Heal cast");
    Wait(
        [&]
        {
            return marine.State().hp() == initialHP;
        },
        "Heal restores HP");
    Wait(
        [&]
        {
            return !marine.State().stimulated();
        },
        "Stim expiry");
    PrintPlayLine("[OK] Slot/range validation, Stim HP cost/cooldown/expiry, channeled Heal");

    const int mp = ghost.State().mp();
    Check(Cast(ghost, 1).ok(), "Cloak cast");
    Wait(
        [&]
        {
            return ghost.State().cloaked() &&
                   Find(*zealot.up_world, ghost.unit.player_id()).player_id().empty();
        },
        "enemy cloak snapshot filtered");
    Check(!Attack(zealot, ghost).ok(), "hidden target cannot be attacked by ID");
    this_thread::sleep_for(chrono::milliseconds(1200));
    Check(ghost.State().mp() <= mp - 26, "cloak drain without MP regen");
    Place(ghost, 112, 55);
    Wait(
        [&]
        {
            return !Find(*zealot.up_world, ghost.unit.player_id()).player_id().empty();
        },
        "building detects cloaked enemy");
    Check(Cast(ghost, 1).ok(), "Cloak off");
    Place(ghost, 51, 50);
    Check(!Cast(ghost, 2, marine.unit.player_id()).ok(), "Lockdown wrong team/filter");
    Check(Cast(ghost, 2, zealot.unit.player_id()).ok(), "Lockdown mechanical enemy fixture");
    Wait(
        [&]
        {
            return zealot.State().locked();
        },
        "Lockdown flag");
    Check(!MoveTo(zealot, 55, 50).ok() && !Attack(zealot, marine).ok(), "Lockdown blocks move/attack");
    Wait(
        [&]
        {
            return !zealot.State().locked();
        },
        "Lockdown expiry");
    Check(Cast(medic, 3, zealot.unit.player_id()).ok(), "OpticalFlare cast");
    Wait(
        [&]
        {
            return zealot.State().blinded() &&
                   Find(*zealot.up_world, marine.unit.player_id()).player_id().empty();
        },
        "blind limits enemy visibility");
    PrintPlayLine("[OK] Cloak cost/drain/detection, hidden-target rejection, Lockdown, OpticalFlare");

    Check(Cast(zergling, 1).ok(), "Burrow cast");
    Wait(
        [&]
        {
            return zergling.State().burrowed();
        },
        "Burrow cast time");
    Check(!MoveTo(zergling, 55, 50).ok() && !Attack(zergling, marine).ok(), "burrow blocks actions");
    Wait(
        [&]
        {
            return Find(*marine.up_world, zergling.unit.player_id()).player_id().empty();
        },
        "burrow hidden");
    this_thread::sleep_for(chrono::milliseconds(1100));
    Check(Cast(zergling, 1).ok(), "Unburrow cast");
    Wait(
        [&]
        {
            return !zergling.State().burrowed();
        },
        "Unburrow complete");
    PrintPlayLine("[OK] Burrow transition, visibility and action restrictions");

    Place(zealot, 57, 51);
    Place(zergling, 45, 45);
    const int hydraHP = hydra.State().hp();
    const int zealotTotal = zealot.State().hp() + zealot.State().shield();
    Check(Cast(templar, 1, "", 57, 50).ok(), "Storm cast");
    Wait(
        [&]
        {
            return hydra.State().hp() == hydraHP - 50;
        },
        "five Storm ticks");
    // 다른 연결의 수신 스레드는 같은 틱을 조금 늦게 받을 수 있다. 각 관측값을 기다린다.
    Wait(
        [&]
        {
            return zealot.State().hp() + zealot.State().shield() == zealotTotal - 50;
        },
        "Storm affects allies and enemies/shields");
    Check(Cast(defiler, 1, "", 57, 50).ok(), "DarkSwarm cast");
    this_thread::sleep_for(chrono::milliseconds(250));
    const int protectedHP = hydra.State().hp();
    Check(Attack(marine, hydra).ok(), "ranged attack under swarm accepted");
    this_thread::sleep_for(chrono::milliseconds(250));
    Check(hydra.State().hp() == protectedHP, "DarkSwarm blocks ranged damage");
    Check(Attack(zealot, hydra).ok(), "melee attack under swarm");
    Wait(
        [&]
        {
            return hydra.State().hp() < protectedHP;
        },
        "DarkSwarm allows melee");
    this_thread::sleep_for(chrono::milliseconds(1100));
    const int mediumHP = hydra.State().hp();
    Check(Attack(ghost, hydra).ok(), "concussive attack");
    Wait(
        [&]
        {
            return hydra.State().hp() == mediumHP - 5;
        },
        "concussive medium 50 percent");
    const int smallHP = marine.State().hp();
    Check(Attack(hydra, marine).ok(), "explosive attack");
    Wait(
        [&]
        {
            return marine.State().hp() == smallHP - 5;
        },
        "explosive small 50 percent");
    PrintPlayLine(
        "[OK] Storm periodic ally/enemy damage, DarkSwarm ranged/melee rules, damage type/size ratios");

    Place(zealot, 45, 55);
    const int beforeOverlap = hydra.State().hp();
    Check(Cast(templar, 1, "", 57, 50).ok(), "first overlapping Storm");
    this_thread::sleep_for(chrono::milliseconds(250));
    Check(Cast(actors[9], 1, "", 57, 50).ok(), "offset overlapping Storm");
    Wait(
        [&]
        {
            return hydra.up_world->Snapshot().areas_size() == 0;
        },
        "overlapping Storm expiry");
    const int overlapDamage = beforeOverlap - hydra.State().hp();
    Check(overlapDamage >= 50 && overlapDamage <= 60, "staggered Storms must not stack damage rate");
    PrintPlayLine("[OK] Different cast times share one Storm damage interval per target");

    Place(zealot, 50, 51);
    const int shield = zealot.State().shield();
    Check(Cast(defiler, 2, "", 50, 50).ok(), "Plague cast");
    Wait(
        [&]
        {
            return marine.State().hp() == 1 && zealot.State().hp() == 1;
        },
        "Plague minimum HP");
    Check(zealot.State().shield() == shield, "Plague bypasses shields");
    Check(Cast(medic, 2, marine.unit.player_id()).ok(), "Restoration cast");
    Check(Cast(medic, 1, marine.unit.player_id()).ok(), "Heal cleansed target");
    Wait(
        [&]
        {
            return marine.State().hp() > 1;
        },
        "cleansed HP can recover");
    wire::SkillRequest cancel;
    cancel.set_cancel(true);
    RequestMessage<wire::ActionReply>(
        *medic.up_world, Protocol::Type::CM_Skill, cancel, Protocol::Type::SM_ActionReply);
    this_thread::sleep_for(chrono::milliseconds(500));
    Check(marine.State().hp() > 1, "Restoration removed ongoing Plague");
    Place(marine, 40, 40);
    Place(medic, 42, 40);
    Place(ghost, 44, 40);
    Place(goliath, 46, 40);
    Check(Cast(templar, 1, "", 50, 51).ok(), "lethal Storm cast");
    Wait(
        [&]
        {
            return !zealot.up_world->ReturnTicket().claims().empty();
        },
        "skill death return ticket saved");
    Check(!zealot.up_world->InWorld(), "skill death leaves World");
    PrintPlayLine(
        "[OK] Plague shield bypass/minimum HP, Restoration, channel stop, skill death and persisted return");
    for (auto& actor : actors)
    {
        if (actor.up_world->InWorld())
        {
            Logout(*actor.up_world);
        }
    }
}
