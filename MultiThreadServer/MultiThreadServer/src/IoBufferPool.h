#pragma once
#include "Network.h"

struct stIocpSession;
enum class IoKind
{
    Accept,
    Receive,
    Send
};

// 완료 통지를 받을 때까지 객체 주소와 버퍼 주소가 바뀌면 안 된다.
struct stIoOperation
{
    OVERLAPPED overlapped{};
    IoKind kind = IoKind::Receive;
    shared_ptr<stIocpSession> sp_session;
    CSocket accepted;
    array<char, 4096> bytes{};
    DWORD length = 0;
};

// 네트워크 I/O 전용 고정 크기 풀이다. Protobuf나 Player의 메모리까지 관리하지 않는다.
// 풀 사용/미사용 모두 같은 Acquire/Release 경로를 사용하여 할당 횟수를 비교한다.
class CIoBufferPool final
{
public:
    CIoBufferPool(bool enabled, size_t capacity);
    ~CIoBufferPool();
    CIoBufferPool(const CIoBufferPool&) = delete;
    CIoBufferPool& operator=(const CIoBufferPool&) = delete;
    stIoOperation* Acquire(IoKind kind);
    void Release(stIoOperation* p_item);
    size_t InUse();
    string Metrics();

private:
    bool m_enabled;
    size_t m_capacity;
    size_t m_requests = 0;
    size_t m_allocated = 0;
    size_t m_reused = 0;
    size_t m_exhausted = 0;
    size_t m_inUse = 0;
    size_t m_peak = 0;
    mutex m_poolMutex; // I/O 풀의 반환 목록과 할당·사용 통계를 보호한다.
    vector<stIoOperation*> m_free;
};
