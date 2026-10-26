#pragma once
#include "Common.h"
#include "Game.pb.h"

namespace Replay
{
constexpr unsigned FormatVersion = 1;
constexpr size_t MaxRecordBytes = 1024 * 1024;
constexpr size_t MaxFileBytes = 1024ULL * 1024 * 1024;
constexpr int MaxUnits = 2048;

// 파일 손상/재생 불일치를 찾는 FNV-1a 체크섬이다. 인증/서명 용도가 아니다.
unsigned long long Checksum(string_view bytes);
wire::WorldState OrderedState(const wire::WorldState& source);
void Apply(const wire::ReplayFrame& frame, wire::WorldState& state);

struct stArchive
{
    wire::ReplayHeader header;
    wire::ReplayFooter footer;
    vector<wire::ReplayFrame> frames;
    vector<size_t> snapshots;
    bool complete = false;
    wire::WorldState StateAt(size_t index) const;
    size_t FindTime(unsigned long long milliseconds) const;
};
stArchive Load(const fs::path& path, bool allowPartial = false);

// 소유 스레드가 상태를 직렬화해서 큐에 넣고 파일 스레드만 디스크를 다룬다.
// 상한/쓰기 오류에서는 해당 기록을 실패 처리하며 전투 스레드를 기다리게 하지 않는다.
class CRecorder final
{
public:
    CRecorder(fs::path directory,
              const wire::ReplayHeader& header,
              size_t queueBytes,
              size_t fileBytes,
              unsigned snapshotTicks);
    ~CRecorder();
    void Action(wire::ReplayAction action);
    void Capture(const wire::WorldState& state);
    void Finish(const string& reason);
    bool Failed() const
    {
        return m_failed.load();
    }
    size_t QueueBytes();

private:
    bool Enqueue(const wire::ReplayRecord& record);
    void Fail(const string& message);
    void Run();
    fs::path m_path;
    unsigned m_tickRate;
    unsigned m_snapshotTicks;
    unsigned long long m_startTick;
    unsigned long long m_sequence = 0;
    unsigned long long m_finalChecksum = 0;
    unsigned long long m_lastSnapshotTick = 0;
    unordered_map<string, string> m_previous;
    vector<wire::ReplayAction> m_actions;
    mutex m_recordMutex; // 리플레이 기록 큐·대기 바이트·종료 상태를 보호한다.
    condition_variable m_ready;
    deque<string> m_queue;
    size_t m_queueBytes = 0;
    size_t m_totalBytes = 8;
    size_t m_queueLimit;
    size_t m_fileLimit;
    bool m_stopping = false;
    bool m_finished = false;
    atomic<bool> m_failed = false;
    jthread m_writer;
};
} // namespace Replay
