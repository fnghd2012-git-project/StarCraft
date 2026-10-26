#pragma once

#include "Common.h"

struct stDatabaseJob
{
    unsigned long long id = 0;
    string action;
    string realm;
    string account;
    string playerId;
    string server;
    string gameServer;
    string token;
    string race;
    string nonce;
    string destination;
    string matchXml;
    fs::path matchFile;
    string password; // 로그인 작업에서만 소유하며 처리 뒤 즉시 지운다. 결과/로그에 싣지 않는다.
    int unitIndex = 0;
    int cooldown = 0;
    int leaseSeconds = 15;
    long long reentryAt = 0;
};

struct stDatabaseResult
{
    unsigned long long id = 0;
    bool ok = false;
    string error;
    string playerId;
    string race;
    int unitIndex = 0;
    long long reentryAt = 0;
    long long leaseUntil = 0;
};

// ODBC 연결은 각 DB 쓰레드(워커)가 독점한다. 게임 스레드와는 값 복사 큐만 공유한다.
class CDatabaseWorker final
{
public:
    CDatabaseWorker(int workerCount, int capacity);
    ~CDatabaseWorker();
    bool Submit(stDatabaseJob job);
    vector<stDatabaseResult> TakeResults();
    size_t Outstanding();

private:
    void Run();
    mutex m_databaseMutex; // DB 작업 큐·결과 큐·처리 대기 수·종료 상태를 보호한다.
    condition_variable m_ready;
    deque<stDatabaseJob> m_jobs;
    vector<stDatabaseResult> m_results;
    vector<jthread> m_workers;
    size_t m_outstanding = 0;
    size_t m_capacity = 0;
    bool m_stopping = false;
    chrono::steady_clock::time_point m_shutdownDeadline;
};
