#include "BattleWorkers.h"

CBattleWorkers::CBattleWorkers(int count) : m_count(count), m_processed(count)
{
    if (count < 1 || count > 16)
    {
        throw runtime_error("Battle workers must be 1..16");
    }
    try
    {
        // 전투 스레드 할당. WorkerCount=1이면 별도 스레드 없이 호출한 메인에서 계산한다.
        if (count > 1)
        {
            for (int i = 0; i < count; ++i)
            {
                m_threads.emplace_back(
                    [this, i]
                    {
                        Work(i);
                    });
            }
        }
    }
    catch (...)
    {
        {
            lock_guard lock(m_battleMutex);
            m_stopping = true;
        }
        m_start.notify_all();
        m_threads.clear();
        throw;
    }
}

CBattleWorkers::~CBattleWorkers()
{
    {
        lock_guard lock(m_battleMutex);
        m_stopping = true;
    }
    m_start.notify_all();
    m_threads.clear();
}

void CBattleWorkers::Run(size_t count, const function<void(size_t)>& calculate)
{
    if (!count)
    {
        return;
    }
    if (m_count == 1)
    {
        for (size_t i = 0; i < count; ++i)
        {
            calculate(i);
        }
        m_processed[0] += count;
        return;
    }
    unique_lock lock(m_battleMutex);
    m_calculate = calculate;
    m_size = count;
    m_error = nullptr;
    m_remaining = m_count;
    ++m_generation;
    // 이번 계산을 워커들에게 알린 뒤, 모두 끝날 때까지 메인은 기다린다.
    m_start.notify_all();
    m_done.wait(lock,
                [this]
                {
                    return m_remaining == 0;
                });
    m_calculate = {};
    if (m_error)
    {
        rethrow_exception(m_error);
    }
}

void CBattleWorkers::Work(int index)
{
    unsigned long long seen = 0;
    unique_lock lock(m_battleMutex);
    for (;;)
    {
        // 새 계산 요청이나 종료 요청이 올 때까지 대기한다. 기다리는 동안 m_battleMutex는 풀린다.
        m_start.wait(lock,
                     [this, seen]
                     {
                         return m_stopping || m_generation != seen;
                     });
        if (m_stopping)
        {
            return;
        }
        seen = m_generation;
        lock.unlock();
        exception_ptr error;
        unsigned long long processed = 0;
        try
        {
            // 서로 다른 결과 칸만 쓴다. stPlayer/건물/랭킹은 이 콜백에서 변경하지 않는다.
            // 예: 워커가 2개면 0번은 0,2,4... 항목, 1번은 1,3,5... 항목을 계산한다.
            for (size_t i = index; i < m_size; i += m_count)
            {
                m_calculate(i);
                ++processed;
            }
        }
        catch (...)
        {
            error = current_exception();
        }
        lock.lock();
        m_processed[index] += processed;
        if (error && !m_error)
        {
            m_error = error;
        }
        // 마지막으로 끝난 워커가 Run에서 기다리는 메인 스레드를 깨운다.
        if (--m_remaining == 0)
        {
            m_done.notify_one();
        }
    }
}

string CBattleWorkers::Metrics() const
{
    // 메인은 Run의 완료 장벽을 지난 뒤에만 읽는다. 워커는 다음 Run까지 대기 중이다.
    string result = "battle_workers=" + to_string(m_count) + " battle_items=";
    for (const auto count : m_processed)
    {
        result += to_string(count) + ",";
    }
    return result;
}
