#include "Network.h"
#include "Database.h"
#include "TcpServer.h"
#include "SpatialCheck.h"
#include "BattleCalculation.h"

static_assert(sizeof(void*) == 8, "Build this project for x64.");
static_assert(__cplusplus >= 202002L, "Enable C++20 and /Zc:__cplusplus.");

namespace
{
void RunStartupChecks()
{
    cout << "MultiThreadServer 기본 환경 점검\n"
         << "플랫폼: x64 / __cplusplus: " << __cplusplus << '\n';
#ifdef _DEBUG
    cout << "빌드: Debug\n";
#else
    cout << "빌드: Release\n";
#endif

    const CWinsockSession winsock;
    cout << "[OK] Winsock 초기화\n";

    const COdbcEnvironment odbc;
    cout << "[OK] ODBC 환경 핸들 생성 (DB 접속 전)\n";

    int workerResult = 0;
    {
        // jthread 소멸 시 합류한다. 합류 후에만 결과를 읽는다.
        jthread worker(
            [&workerResult]
            {
                workerResult = 1;
            });
    }
    if (workerResult != 1)
    {
        throw runtime_error("Worker result check failed.");
    }
    cout << "[OK] C++20 jthread 생성 및 합류\n"
         << "하드웨어 스레드 힌트: " << thread::hardware_concurrency() << " (0이면 확인 불가)\n"
         << "기본 환경 점검 완료.\n";
}

void RunDatabaseChecks()
{
    const stDatabaseConfig config = LoadDatabaseConfig();
    CDatabaseConnection connection(config);
    const stDatabaseCheckResult result = connection.CheckConnection();

    cout << "[OK] MSSQL ODBC 연결 및 조회\n"
         << "서버: " << result.serverName << " / 버전: " << result.productVersion << '\n'
         << "DB: " << result.databaseName << " / 전송: " << result.transport
         << " / 인증: " << result.authentication << '\n'
         << "[OK] DB 이름, 정수 1, 한글 조회 결과 확인\n";
}
} // namespace

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc == 2 && string_view(argv[1]) == "--battle-check")
    {
        return RunBattleChecks();
    }
    if (argc >= 2 && string_view(argv[1]) == "--spatial-check")
    {
        if (argc == 2)
        {
            return RunSpatialChecks({});
        }
        if (argc == 4 && string_view(argv[2]) == "--csv")
        {
            return RunSpatialChecks(fs::path(argv[3]));
        }
        cerr << "Usage: --spatial-check [--csv output.csv]\n";
        return 2;
    }

    if (argc == 2 && string_view(argv[1]) == "--help")
    {
        cout << "Usage: MultiThreadServer.exe [--self-check | --db-check | --help]\n"
             << "       MultiThreadServer.exe --server CSV_Index|World [--config Server.ini] [--run-seconds "
                "N]\n"
             << "       MultiThreadServer.exe --config-check CSV_Index|World [--config Server.ini]\n"
             << "       MultiThreadServer.exe --spatial-check [--csv output.csv]\n"
             << "       MultiThreadServer.exe --battle-check\n"
             << "인수 없음: 기본 환경과 DB 연결을 점검하고 Enter를 기다립니다.\n"
             << "--self-check: DB 접속 없이 기본 환경만 점검합니다.\n"
             << "--db-check: 기본 환경과 DB 연결을 점검하고 대기 없이 종료합니다.\n";
        return 0;
    }

    if (argc >= 2 && (string_view(argv[1]) == "--server" || string_view(argv[1]) == "--config-check"))
    {
        try
        {
            if (argc < 3)
            {
                throw runtime_error("Missing server id");
            }
            fs::path configPath = "Server.ini";
            int runSeconds = 0;
            for (int i = 3; i < argc; i += 2)
            {
                if (i + 1 >= argc)
                {
                    throw runtime_error("Missing option value");
                }
                if (string_view(argv[i]) == "--config")
                {
                    configPath = fs::path(argv[i + 1]);
                }
                else if (string_view(argv[i]) == "--run-seconds" && string_view(argv[1]) == "--server")
                {
                    runSeconds = ParseInt(argv[i + 1], 1, 86400);
                }
                else
                {
                    throw runtime_error("Unknown server option");
                }
            }
            const auto config = LoadServerConfig(configPath, argv[2]);
            const auto map = LoadMapConfig(config.mapFile, config.startPointFile);
            const auto data = LoadGameData(config);
            if (string_view(argv[1]) == "--config-check")
            {
                cout << "[OK] Configuration / map / CSV\n" << MakeServerInfo(config, map, data);
            }
            else
            {
                RunTcpServer(config, map, data, runSeconds);
            }
            return 0;
        }
        catch (const exception& error)
        {
            cerr << "[FAIL] " << error.what() << '\n';
            return 1;
        }
    }

    const bool selfCheck = argc == 2 && string_view(argv[1]) == "--self-check";
    const bool dbCheck = argc == 2 && string_view(argv[1]) == "--db-check";
    if (argc > 1 && !selfCheck && !dbCheck)
    {
        cerr << "지원하지 않는 인수입니다. --help를 확인하세요.\n";
        return 2;
    }

    int exitCode = 0;
    try
    {
        RunStartupChecks();
        if (!selfCheck)
        {
            RunDatabaseChecks();
        }
    }
    catch (const exception& error)
    {
        cerr << "[FAIL] " << error.what() << '\n';
        exitCode = 1;
    }

    if (argc == 1)
    {
        cout << "종료하려면 Enter를 누르세요.\n";
        cin.get();
    }
    return exitCode;
}
