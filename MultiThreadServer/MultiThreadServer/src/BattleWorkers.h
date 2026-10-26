#pragma once
#include "Common.h"

// 한 전장의 읽기 전용 계산을 나눈다. Run이 끝나야 메인이 결과를 반영한다.
// 1이면 호출한 메인에서 실행한다. 2 이상이면 생성해 둔 워커를 계속 재사용한다.
class CBattleWorkers final
{
public:
    explicit CBattleWorkers(int count);
    ~CBattleWorkers();
    void Run(size_t count, const function<void(size_t)>& calculate);
    int Count() const
    {
        return m_count;
    }
    string Metrics() const;

private:
    void Work(int index);
    int m_count;
    mutex m_battleMutex; // 전투 작업 세대·계산 함수·완료 수를 메인과 전투 쓰레드(워커)가 공유한다.
    condition_variable m_start;
    condition_variable m_done;
    vector<jthread> m_threads;
    function<void(size_t)> m_calculate;
    size_t m_size = 0;
    unsigned long long m_generation = 0;
    vector<unsigned long long> m_processed;
    int m_remaining = 0;
    bool m_stopping = false;
    exception_ptr m_error;
};
