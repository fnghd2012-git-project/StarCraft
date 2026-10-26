#pragma once
#include "BattleDecision.h"
#include "ClientConnection.h"

using BattleClock = chrono::steady_clock;
enum class BattleStage
{
    Waiting,
    Game,
    World,
    Done,
    Failed
};
enum class BattlePending
{
    None,
    ServerVersionCheck,
    Login,
    Ticket,
    Enter,
    Resume,
    Action,
    Logout
};

struct stBattleCounters
{
    atomic<int> admitting = 0;
    atomic<int> ready = 0;
    atomic<int> world = 0;
    atomic<int> entered = 0;
    atomic<int> deaths = 0;
    atomic<int> reentries = 0;
    atomic<int> resumed = 0;
    atomic<int> errors = 0;
    atomic<int> done = 0;
    atomic<unsigned long long> unitAttacks = 0;
    atomic<unsigned long long> buildingAttacks = 0;
    atomic<unsigned long long> skills = 0;
    atomic<unsigned long long> rejected = 0;
    atomic<bool> started = false;
    atomic<bool> stopping = false;
};

struct stBattleBot
{
    int index = 0;
    const stServerConfig* p_game = nullptr;
    const stServerConfig* p_endpoint = nullptr;
    CSocket socket;
    BattleStage stage = BattleStage::Waiting;
    BattlePending pending = BattlePending::None;
    bool connecting = false;
    bool initialSlot = false;
    bool prepared = false;
    bool died = false;
    bool entryDenied = false;
    unsigned generation = 0;
    string account;
    string recvBuffer;
    string sendBuffer;
    size_t sendOffset = 0;
    wire::WorldTicket ticket;
    wire::UnitState unit;
    wire::WorldState snapshot, assembling;
    wire::MapInfo map;
    vector<wire::SkillInfo> skills;
    CBattleDecision decision;
    stBattleOrder order;
    BattleClock::time_point deadline{};
    BattleClock::time_point retry{};
    BattleClock::time_point nextDecision{};
};

// 대규모 시연의 소켓들을 한 워커에서 처리한다. Bot별 스레드를 만들지 않는다.
class CBattleWorker
{
public:
    CBattleWorker(stBattleCounters& counters, const stServerConfig& world, int admission);
    vector<unique_ptr<stBattleBot>> m_bots;
    void Run(stop_token stop);

private:
    void QueuePacket(stBattleBot& bot,
                     Protocol::Type type,
                     const google::protobuf::MessageLite& message,
                     BattlePending pending);
    void Connect(stBattleBot& bot, const stServerConfig& endpoint);
    void HandlePacket(stBattleBot& bot, Protocol::Type type, const string& payload);
    void Receive(stBattleBot& bot);
    void UpdateBot(stBattleBot& bot);
    void Fail(stBattleBot& bot, const string& message);
    stBattleCounters& m_counters;
    const stServerConfig& m_world;
    int m_admission;
};
