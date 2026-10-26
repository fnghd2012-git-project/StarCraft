#include "IoBufferPool.h"
#include "IocpSession.h"

CIoBufferPool::CIoBufferPool(bool enabled, size_t capacity) : m_enabled(enabled), m_capacity(capacity)
{
    if (m_enabled)
    {
        m_free.reserve(m_capacity);
    }
}

CIoBufferPool::~CIoBufferPool()
{
    for (auto* p_item : m_free)
    {
        delete p_item;
    }
}

stIoOperation* CIoBufferPool::Acquire(IoKind kind)
{
    // 여러 IOCP 쓰레드(워커)가 함께 쓰는 풀. 요청 하나마다 OVERLAPPED와 송수신 버퍼를 빌린다.
    lock_guard lock(m_poolMutex);
    ++m_requests;
    stIoOperation* p_item = nullptr;
    if (m_enabled && !m_free.empty())
    {
        p_item = m_free.back();
        m_free.pop_back();
        ++m_reused;
    }
    else
    {
        // 풀 사용 시 최대 개수를 넘겨 할당하지 않는다. 비활성화 상태에서는 매번 새로 할당한다.
        if (m_enabled && m_allocated >= m_capacity)
        {
            ++m_exhausted;
            return nullptr;
        }
        p_item = new stIoOperation;
        ++m_allocated;
    }
    p_item->overlapped = {};
    p_item->kind = kind;
    p_item->length = 0;
    ++m_inUse;
    m_peak = max(m_peak, m_inUse);
    return p_item;
}

void CIoBufferPool::Release(stIoOperation* p_item)
{
    // I/O 완료를 회수했거나 요청 등록 자체가 실패했을 때만 반환한다.
    p_item->sp_session.reset();
    p_item->accepted.Reset();
    lock_guard lock(m_poolMutex);
    --m_inUse;
    if (m_enabled)
    {
        // 다음 요청이 재사용하도록 보관한다. 풀을 끄면 아래에서 즉시 delete한다.
        m_free.push_back(p_item);
    }
    else
    {
        delete p_item;
    }
}

size_t CIoBufferPool::InUse()
{
    lock_guard lock(m_poolMutex);
    return m_inUse;
}

string CIoBufferPool::Metrics()
{
    lock_guard lock(m_poolMutex);
    ostringstream text;
    text << "pool=" << m_enabled << " pool_requests=" << m_requests << " pool_allocations=" << m_allocated
         << " pool_reused=" << m_reused << " pool_exhausted=" << m_exhausted << " pool_in_use=" << m_inUse
         << " pool_peak=" << m_peak << " pool_cached=" << m_free.size()
         << " pool_block_bytes=" << sizeof(stIoOperation);
    return text.str();
}
