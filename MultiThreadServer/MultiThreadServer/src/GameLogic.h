#pragma once

#include "GameData.h"
#include "EntryTicket.h"
#include "ChatRelay.h"
#include "DatabaseWorker.h"
#include "Replay.h"
#include "RedisWorker.h"
#include "BattleWorkers.h"
#include "BattleCalculation.h"

// 네트워크 쓰레드(워커)는 소켓만 소유한다. 메인 스레드와 패킷을 교환하는 작은 우편함이다.
struct stGameChannel
{
    atomic<bool> connected = true;       // 이 연결을 계속 사용할 수 있는지 확인
    atomic<bool> closeAfterSend = false; // 마지막 응답을 보낸 뒤 연결을 종료하도록 표시
    atomic<int> pendingCommands = 0; // 이 연결이 등록했지만 아직 처리를 시작하지 않은 게임 명령 수.
    mutex sendMutex; // 메인과 네트워크 쓰레드(워커)가 응답 큐를 동시에 건드리지 않도록 보호
    vector<string> sendQueue; // 네트워크 쓰레드(워커)에 넘길 완성된 응답 패킷.
    size_t pendingSendBytes = 0; // sendQueue의 현재 대기 바이트 수. 큐를 비우면 0, 누적 전송량과 다르다.
    // IOCP는 응답 도착을 완료 포트에 알린다. sendMutex 안에서만 읽고 변경한다.
    function<void()> notifySend; // IOCP 쓰레드(워커)에 “보낼 응답이 생겼다”고 알림
    bool sendNotifyPending = false; // 이미 알렸다면 같은 알림을 중복해서 보내지 않도록 표시
};

class CGameLogic final
{
public:
    CGameLogic(const stServerConfig& config, const stMapConfig& map, const stGameData& data);
    ~CGameLogic();
    // 네트워크 쓰레드(워커) -> 메인 스레드 전달 지점. 본문을 명령 큐에 복사한다.
    void Submit(const shared_ptr<stGameChannel>& sp_channel, Protocol::Type type, string_view payload);
    // RunTcpServer의 메인 루프에서 실행한다. 게임 상태 반영은 이 스레드가 담당한다.
    void Update();

private:
    // 명령 큐 상한과 한 번의 Update에서 처리할 개수. 클라이언트 독점과 틱 지연을 제한한다.
    static constexpr size_t MaxPendingCommands = 2048;
    static constexpr int MaxPendingCommandsPerClient = 32;
    static constexpr int MaxCommandsPerUpdate = 128;

    struct stCommand
    {
        shared_ptr<stGameChannel> sp_channel;
        Protocol::Type type;
        string payload; // 헤더를 제외한 Protobuf 본문. HandlePacket에서 메시지로 복원한다.
    };
    struct stPeriodicEffect
    {
        const stSkillDefinition* p_skill = nullptr;
        unsigned long long until = 0;
        unsigned long long next = 0;
    };
    struct stPlayer
    {
        shared_ptr<stGameChannel> sp_channel;
        wire::UnitState unit;
        const stUnitDefinition* p_definition = nullptr;
        bool inWorld = false;
        chrono::steady_clock::time_point nextRankQuery{};
        bool moving = false;
        double goalX = 0;
        double goalY = 0;
        chrono::steady_clock::time_point nextAttack{};
        chrono::steady_clock::time_point nextChat{};
        long long reentryAt = 0;
        string databaseToken;
        long long leaseUntil = 0;
        long long renewAt = 0;
        bool databaseBusy = false;
        bool renewing = false;
        const stSkillDefinition* p_stim = nullptr;
        const stSkillDefinition* p_cloak = nullptr;
        const stSkillDefinition* p_heal = nullptr;
        unsigned long long stimUntil = 0;
        unsigned long long lockedUntil = 0;
        unsigned long long burrowAt = 0;
        unsigned long long healAt = 0;
        unsigned long long cloakAt = 0;
        unsigned long long nextHeal = 0;
        unsigned long long nextStormDamage = 0;
        bool burrowTarget = false;
        int blindSight = 1;
        string healTarget;
        unordered_map<int, unsigned long long> skillCooldowns;
        stPeriodicEffect plague;
        string relayServerId;
        string relayClientNonce;
        string relayNonce;
        bool relayReady = false;
        chrono::steady_clock::time_point relayDeadline{};
    };
    struct stPendingChat
    {
        shared_ptr<stGameChannel> sp_channel;
        chrono::steady_clock::time_point deadline;
    };
    struct stPendingDatabase
    {
        shared_ptr<stGameChannel> sp_channel;
        string kind;
        string token;
        string payload;
        string reason;
        wire::WorldTicket ticket;
        wire::WorldReply deniedReply;
        unsigned retryAfter = 0;
    };
    // DB 요청/결과와 계정 세션: GamePersistence.cpp
    stDatabaseJob MakeDatabaseJob(const stPlayer& player, const string& action) const;
    void SubmitDatabase(stPlayer& player, stDatabaseJob job, stPendingDatabase pending);
    void UpdateDatabase();
    void Login(stPlayer& player, const wire::JoinGameRequest& request);
    void ValidateGameCapacity(const stPlayer& player) const;
    void AcceptTransfer(stPlayer& player, const wire::EnterWorldRequest& request, bool world);
    void RequestTransfer(stPlayer& player);
    void SaveReturn(stPlayer& player,
                    const string& reason,
                    long long reentryAt,
                    const wire::WorldReply& denied = {});
    void Logout(stPlayer& player);
    // 패킷 명령 처리: GameLogic.cpp, 스킬 계산: GameSkills.cpp
    void HandlePacket(stPlayer& player, Protocol::Type type, const string& payload);
    void HandleGamePacket(stPlayer& player, Protocol::Type type, const string& payload);
    void HandleWorldPacket(stPlayer& player, Protocol::Type type, const string& payload);
    void HandleMove(stPlayer& player, const wire::MoveRequest& request);
    void EnterWorld(stPlayer& player, const wire::EnterWorldRequest& request);
    void HandleAttack(stPlayer& player, const string& targetId);
    void HandleSkill(stPlayer& player, const wire::SkillRequest& request);
    void UpdateSkills();
    bool CanSee(const stPlayer& viewer, const stPlayer& target) const;
    bool InDarkSwarm(const stPlayer& target) const;
    bool SkillAffects(const stSkillDefinition& skill, wire::Race source, const stPlayer& target) const;
    void ClearCombat(stPlayer& player);
    unsigned long long SkillTicks(int milliseconds) const;
    // 일반/월드 채팅과 서버 간 중계: GameLogic.cpp
    void HandleChat(stPlayer& player, const wire::ChatRequest& request);
    void BroadcastChat(const wire::ChatMessage& message);
    void HandleRelay(stPlayer& player, Protocol::Type type, const string& payload);
    void UpdateChatRelay();
    // 전장 시간 진행: Tick → MovePlayers / UpdateSkills → BroadcastWorldState
    void Tick();
    void MovePlayers();
    stPlayer* FindPlayer(const string& id) const;
    void BuildCombatIndex();
    vector<size_t> FindTargetsInRange(double x, double y, double radius) const;
    void BroadcastWorldState();
    vector<const stPlayer*> SelectSnapshotPlayers(const stPlayer& receiver,
                                                  const vector<const stPlayer*>& players,
                                                  const vector<size_t>& candidates) const;
    void ReturnToGame(stPlayer& player, const string& reason, bool applyCooldown);
    // 경기 통계와 Redis 순위: GameRanking.cpp
    void StartMatch();
    void FinishMatch(const string& reason);
    void CountEntry(const stPlayer& player);
    void CountDeath(const stPlayer& victim, const string& killerId, wire::Race killerRace);
    void PublishRanking();
    void RequestRanking(stPlayer& player, const string& payload);
    void UpdateRanking();
    void ResetBuildings();
    // 리플레이 기록 요청: GameReplay.cpp
    void StartReplay();
    void CaptureReplay();
    void RecordReplayCommand(const stPlayer& player,
                             Protocol::Type type,
                             const string& payload,
                             bool accepted,
                             const string& error);
    void RecordReplayEvent(const stPlayer& player, const string& kind, const string& message);
    void ReportMetrics();
    wire::WorldTicket CreateReturnTicket(const wire::UnitState& unit, long long reentryAt);
    void SendMessage(const shared_ptr<stGameChannel>& sp_channel,
                     Protocol::Type type,
                     const google::protobuf::MessageLite& message);
    void SendPacket(const shared_ptr<stGameChannel>& sp_channel, string packet);
    void SendActionReply(stPlayer& player, bool ok, const string& message);
    void BroadcastWorldEvent(const string& message);
    wire::UnitState MakeUnit(const stUnitDefinition& unit,
                             const string& id,
                             const string& name,
                             const string& gameServer) const;
    bool IsValidPosition(double x, double y) const;
    // 설정은 시작 시 읽고, 플레이어·건물 상태의 최종 변경은 메인 쓰레드만 수행한다.
    const stServerConfig& m_config;
    const stMapConfig& m_map;
    const stGameData& m_data;
    CEntryTicket m_tickets;
    CDatabaseWorker m_database;
    CBattleWorkers m_battleWorkers;
    // 메인이 인덱스를 만들고 워커 작업이 끝날 때까지 stPlayer 수명과 좌표를 고정한다.
    unordered_map<string, stPlayer*> m_playersById;
    vector<stPlayer*> m_combatPlayers;
    CSpatialIndex m_combatIndex;
    bool m_combatIndexDirty = true;
    unsigned long long m_parallelVerified = 0;
    long long m_battleCalculateMicros = 0;
    long long m_battleApplyMicros = 0;
    unique_ptr<CRedisWorker> m_up_redis;
    string m_matchId;
    unordered_map<string, wire::RankEntry> m_ranking;
    bool m_rankingDirty = true;
    bool m_matchFinished = false;
    unsigned long long m_rankingRevision = 0;
    unsigned long long m_nextRankQuery = 0;
    chrono::steady_clock::time_point m_nextRankPublish{};
    unordered_map<unsigned long long, shared_ptr<stGameChannel>> m_pendingRanks;
    // 실패한 경기 결과는 로컬 보관 파일에서 수동 재처리할 수 있도록 함께 남긴다.
    unordered_map<unsigned long long, fs::path> m_pendingMatchSaves;
    unique_ptr<Replay::CRecorder> m_up_replay;
    bool m_replayStartupFailed = false;
    vector<wire::ReplayDataFile> m_replayData;
    vector<long long> m_tickMicros;
    vector<long long> m_snapshotMicros;
    CSpatialIndex m_spatialIndex;
    unsigned long long m_spatialQueries = 0;
    unsigned long long m_spatialCandidates = 0;
    unsigned long long m_spatialVerified = 0;
    long long m_spatialBuildMicros = 0;
    long long m_spatialSelectMicros = 0;
    long long m_snapshotSendMicros = 0;
    unsigned long long m_commandCount = 0;
    unsigned long long m_commandRejected = 0;
    unsigned long long m_missedTicks = 0;
    size_t m_commandQueuePeak = 0;
    chrono::steady_clock::time_point m_metricsAt = chrono::steady_clock::now();
    unordered_map<unsigned long long, stPendingDatabase> m_pendingDatabase;
    unsigned long long m_nextDatabaseId = 0;
    unique_ptr<CChatRelay> m_up_chatRelay;
    unordered_map<string, stPendingChat> m_pendingChats;
    mt19937 m_random{random_device{}()};
    // IOCP 쓰레드(워커)와 메인이 함께 접근하는 수신 명령 큐. 꺼낸 뒤에는 락을 풀고 처리한다.
    mutex m_commandsMutex;
    deque<stCommand> m_commands;
    unordered_map<stGameChannel*, stPlayer> m_players;
    unordered_map<string, long long> m_usedTickets;
    unordered_map<string, chrono::steady_clock::time_point> m_reentry;
    array<wire::BuildingState, 3> m_buildings;
    struct stAreaEffect
    {
        const stSkillDefinition* p_skill;
        wire::Race race;
        double x;
        double y;
        unsigned long long until;
        unsigned long long next;
        string sourcePlayerId;
    };
    vector<stAreaEffect> m_areas;
    array<stPeriodicEffect, 3> m_buildingPlagues;
    chrono::steady_clock::time_point m_nextTick = chrono::steady_clock::now();
    chrono::steady_clock::time_point m_restartAt{};
    unsigned long long m_tick = 0;
    bool m_roundRunning = true;
    wire::Race m_winner = wire::RACE_UNSPECIFIED;
};
