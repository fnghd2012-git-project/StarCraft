#include "DatabaseWorker.h"
#include "Database.h"
#include "Config.h"
#include "EntryTicket.h"
#include <bcrypt.h>

namespace
{
string PasswordHash(const string& password, const string& salt)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
    {
        throw runtime_error("Password algorithm initialization failed");
    }
    array<unsigned char, 32> bytes{};
    const auto result = BCryptDeriveKeyPBKDF2(algorithm,
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                                              static_cast<ULONG>(password.size()),
                                              reinterpret_cast<PUCHAR>(const_cast<char*>(salt.data())),
                                              static_cast<ULONG>(salt.size()),
                                              600000,
                                              bytes.data(),
                                              static_cast<ULONG>(bytes.size()),
                                              0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0)
    {
        throw runtime_error("Password derivation failed");
    }
    constexpr char hex[] = "0123456789abcdef";
    string hash;
    for (const auto byte : bytes)
    {
        hash += hex[byte >> 4];
        hash += hex[byte & 15];
    }
    SecureZeroMemory(bytes.data(), bytes.size());
    return hash;
}

void ErasePassword(string& password)
{
    if (!password.empty())
    {
        SecureZeroMemory(password.data(), password.size());
        password.clear();
    }
}
} // namespace

CDatabaseWorker::CDatabaseWorker(int workerCount, int capacity) : m_capacity(capacity)
{
    try
    {
        // DB 스레드 할당. Server.ini의 [Database] WorkerCount만큼 생성한다.
        for (int i = 0; i < workerCount; ++i)
        {
            m_workers.emplace_back(
                [this]
                {
                    Run();
                });
        }
    }
    catch (...)
    {
        // 일부 스레드만 생성된 경우에도 대기 조건을 풀어야 jthread 소멸 시 join이 끝난다.
        {
            lock_guard lock(m_databaseMutex);
            m_stopping = true;
        }
        m_ready.notify_all();
        m_workers.clear();
        throw;
    }
}

CDatabaseWorker::~CDatabaseWorker()
{
    {
        lock_guard lock(m_databaseMutex);
        m_stopping = true;
        m_shutdownDeadline = chrono::steady_clock::now() + chrono::seconds(5);
    }
    m_ready.notify_all();
    m_workers.clear();
    // 종료 제한 시간 이후 남은 작업은 성공 처리하지 않는다. DB 세션 만료로 복구한다.
    for (auto& job : m_jobs)
    {
        ErasePassword(job.password);
    }
    if (!m_jobs.empty())
    {
        cerr << "[DB] Shutdown left " << m_jobs.size() << " uncommitted jobs; session leases will expire"
             << endl;
    }
}

bool CDatabaseWorker::Submit(stDatabaseJob job)
{
    // 메인에서는 DB 작업만 등록한다. 실제 쿼리는 Run의 DB 쓰레드(워커)가 실행한다.
    lock_guard lock(m_databaseMutex);
    // 경기 결과 한 건의 예약 슬롯을 둔다. 일반 세션 큐가 꽉 차도 마지막 결과를 우선 처리한다.
    const bool match = job.action == "SaveMatch";
    if (m_stopping || m_outstanding >= m_capacity + (match ? 1 : 0))
    {
        ErasePassword(job.password);
        return false;
    }
    if (match)
    {
        m_jobs.push_front(move(job));
    }
    else
    {
        m_jobs.push_back(move(job));
    }
    ++m_outstanding;
    m_ready.notify_one();
    return true;
}

vector<stDatabaseResult> CDatabaseWorker::TakeResults()
{
    // 완료 결과를 메인으로 넘긴다. 플레이어 상태와 응답 처리는 GameLogic에서 이어서 한다.
    lock_guard lock(m_databaseMutex);
    vector<stDatabaseResult> result;
    result.swap(m_results);
    m_outstanding -= result.size();
    return result;
}

size_t CDatabaseWorker::Outstanding()
{
    lock_guard lock(m_databaseMutex);
    return m_outstanding;
}

void CDatabaseWorker::Run()
{
    // 워커마다 DB 연결 하나를 소유한다. 여러 워커가 같은 연결을 동시에 쓰지 않는다.
    unique_ptr<CDatabaseConnection> up_connection;
    while (true)
    {
        stDatabaseJob job;
        {
            unique_lock lock(m_databaseMutex);
            m_ready.wait(lock,
                         [this]
                         {
                             return m_stopping || !m_jobs.empty();
                         });
            if (m_stopping && (m_jobs.empty() || chrono::steady_clock::now() >= m_shutdownDeadline))
            {
                return;
            }
            job = move(m_jobs.front());
            m_jobs.pop_front();
        }
        stDatabaseResult result;
        result.id = job.id;
        // 작업 큐 잠금은 이미 해제했다. DB 응답을 기다리는 동안에도 새 작업을 등록할 수 있다.
        // 같은 Token/Nonce의 DB 작업은 멱등이다. 연결/교착 오류만 한 번 재연결해서 재시도한다.
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            try
            {
                if (job.action == "SaveMatch" && attempt == 0)
                {
                    fs::create_directories(job.matchFile.parent_path());
                    ofstream backup(job.matchFile, ios::binary);
                    backup << job.matchXml;
                    backup.close();
                    if (!backup)
                    {
                        throw runtime_error("Cannot write match result backup");
                    }
                }
                if (!up_connection)
                {
                    auto config = LoadDatabaseConfig();
                    config.loginTimeoutSeconds = 2;
                    up_connection = make_unique<CDatabaseConnection>(config);
                }
                if (job.action == "SaveMatch")
                {
                    up_connection->SaveMatch(job.matchXml);
                    result.ok = true;
                    cout << "[MATCH DB] committed " << job.matchFile.filename().string() << endl;
                    break;
                }
                vector<string> parameters = {job.action,
                                             job.realm,
                                             job.account,
                                             job.playerId,
                                             job.server,
                                             job.gameServer,
                                             job.token,
                                             to_string(job.unitIndex),
                                             job.race,
                                             job.nonce,
                                             job.destination,
                                             to_string(job.reentryAt),
                                             to_string(job.cooldown),
                                             to_string(job.leaseSeconds),
                                             "",
                                             ""};
                if (job.action == "Login")
                {
                    parameters[0] = "Auth";
                    string salt = up_connection->ExecuteSession(parameters)[0];
                    if (salt.empty())
                    {
                        salt = CEntryTicket::NewId();
                    }
                    parameters[0] = "Login";
                    parameters[14] = salt;
                    parameters[15] = PasswordHash(job.password, salt);
                }
                const auto values = up_connection->ExecuteSession(parameters);
                result.playerId = values[0];
                result.unitIndex = ParseInt(values[1], 1, 1000000);
                result.race = values[2];
                result.reentryAt = stoll(values[3]);
                result.leaseUntil = stoll(values[4]);
                result.ok = true;
                break;
            }
            catch (const exception& error)
            {
                result.error = error.what();
                const bool retry = result.error.find("SQLSTATE=08") != string::npos ||
                                   result.error.find("SQLSTATE=HYT") != string::npos ||
                                   result.error.find("SQLSTATE=40001") != string::npos;
                up_connection.reset();
                if (!retry)
                {
                    break;
                }
            }
        }
        if (job.action == "SaveMatch" && !result.ok)
        {
            cerr << "[MATCH DB FAIL] " << result.error << "; backup path=" << job.matchFile.string() << endl;
        }
        ErasePassword(job.password);
        {
            // 실행 결과를 완료 큐에 넣고 다음 작업으로 이동한다.
            lock_guard lock(m_databaseMutex);
            m_results.push_back(move(result));
        }
    }
}
