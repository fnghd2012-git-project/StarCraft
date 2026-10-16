#include "Common.h"
#include "Database.h"

namespace
{
    string ToUtf8(wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int length = static_cast<int>(text.size());
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text.data(), length, nullptr, 0, nullptr, nullptr);
        if (bytes == 0)
        {
            throw runtime_error("UTF-8 conversion failed.");
        }

        string result(bytes, '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            text.data(), length, result.data(), bytes, nullptr, nullptr) == 0)
        {
            throw runtime_error("UTF-8 conversion failed.");
        }
        return result;
    }

    wstring ReadEnvironment(const wchar_t* name, const wstring& defaultValue)
    {
        array<wchar_t, 512> value{};
        const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
        if (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        {
            return defaultValue;
        }
        if (length == 0 || length >= value.size())
        {
            throw runtime_error("Invalid environment setting: " + ToUtf8(name));
        }
        return wstring(value.data(), length);
    }

    wstring QuoteConnectionValue(const wstring& value)
    {
        wstring result = L"{";
        for (const wchar_t ch : value)
        {
            result += ch;
            if (ch == L'}')
            {
                result += L'}';
            }
        }
        return result + L'}';
    }

    string ReadDiagnostics(SQLSMALLINT handleType, SQLHANDLE handle)
    {
        string result;
        array<SQLWCHAR, 1024> message{};
        for (int record = 1; record <= (numeric_limits<SQLSMALLINT>::max)(); ++record)
        {
            SQLWCHAR state[6]{};
            SQLINTEGER nativeError = 0;
            SQLSMALLINT messageLength = 0;
            const SQLRETURN status = SQLGetDiagRecW(handleType, handle,
                static_cast<SQLSMALLINT>(record), state, &nativeError, message.data(),
                static_cast<SQLSMALLINT>(message.size()), &messageLength);
            if (status == SQL_NO_DATA)
            {
                break;
            }
            if (!SQL_SUCCEEDED(status))
            {
                result += "\nODBC diagnostic retrieval failed.";
                break;
            }

            result += "\nSQLSTATE=" + ToUtf8(state) + " NativeError=" + to_string(nativeError)
                + " " + ToUtf8(message.data());
            if (status == SQL_SUCCESS_WITH_INFO)
            {
                result += " [diagnostic truncated]";
            }
        }
        return result;
    }

    void CheckOdbc(SQLRETURN result, SQLSMALLINT handleType, SQLHANDLE handle, const char* operation)
    {
        if (!SQL_SUCCEEDED(result))
        {
            throw runtime_error(string(operation) + " failed." + ReadDiagnostics(handleType, handle));
        }
        if (result == SQL_SUCCESS_WITH_INFO)
        {
            cerr << "[ODBC INFO] " << operation << ReadDiagnostics(handleType, handle) << '\n';
        }
    }

    class OdbcStatement final
    {
    public:
        explicit OdbcStatement(SQLHDBC connection)
        {
            CheckOdbc(SQLAllocHandle(SQL_HANDLE_STMT, connection, &handle_),
                SQL_HANDLE_DBC, connection, "SQLAllocHandle(STMT)");
        }

        ~OdbcStatement()
        {
            SQLFreeHandle(SQL_HANDLE_STMT, handle_);
        }

        OdbcStatement(const OdbcStatement&) = delete;
        OdbcStatement& operator=(const OdbcStatement&) = delete;

        SQLHSTMT GetHandle() const { return handle_; }

    private:
        SQLHSTMT handle_ = SQL_NULL_HSTMT;
    };

    wstring ReadCheckText(SQLHSTMT statement, SQLUSMALLINT column)
    {
        // 연결 점검 쿼리의 문자열은 모두 nvarchar(128) 이하다.
        array<SQLWCHAR, 256> value{};
        SQLLEN bytes = 0;
        CheckOdbc(SQLGetData(statement, column, SQL_C_WCHAR, value.data(), sizeof(value), &bytes),
            SQL_HANDLE_STMT, statement, "SQLGetData(text)");
        if (bytes < 0 || bytes >= static_cast<SQLLEN>(sizeof(value)))
        {
            throw runtime_error("Connection check returned NULL or truncated text.");
        }
        return wstring(value.data(), static_cast<size_t>(bytes) / sizeof(SQLWCHAR));
    }
}

DatabaseConfig LoadDatabaseConfig()
{
    DatabaseConfig config;
    config.driver = ReadEnvironment(L"MTS_DB_DRIVER", config.driver);
    config.server = ReadEnvironment(L"MTS_DB_SERVER", config.server);
    config.database = ReadEnvironment(L"MTS_DB_NAME", config.database);

    // 기본 로컬 테스트 서버의 자체 서명 인증서만 기본 허용한다.
    const wstring trust = ReadEnvironment(L"MTS_DB_TRUST_SERVER_CERTIFICATE",
        config.server == L"tcp:127.0.0.1,1433" ? L"yes" : L"no");
    if (trust != L"yes" && trust != L"no")
    {
        throw runtime_error("MTS_DB_TRUST_SERVER_CERTIFICATE must be yes or no.");
    }
    config.trustServerCertificate = trust == L"yes";
    return config;
}

OdbcEnvironment::OdbcEnvironment()
{
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &handle_)))
    {
        throw runtime_error("SQLAllocHandle(ENV) failed.");
    }

    try
    {
        CheckOdbc(SQLSetEnvAttr(handle_, SQL_ATTR_ODBC_VERSION,
            reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(SQL_OV_ODBC3)), 0),
            SQL_HANDLE_ENV, handle_, "SQLSetEnvAttr(ODBC3)");
    }
    catch (...)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, handle_);
        throw;
    }
}

OdbcEnvironment::~OdbcEnvironment()
{
    SQLFreeHandle(SQL_HANDLE_ENV, handle_);
}

DatabaseConnection::DatabaseConnection(const DatabaseConfig& config)
    : databaseName_(config.database)
{
    wstring connectionString = L"DRIVER=" + QuoteConnectionValue(config.driver)
        + L";SERVER=" + QuoteConnectionValue(config.server)
        + L";DATABASE=" + QuoteConnectionValue(config.database)
        + L";Trusted_Connection=yes;Encrypt=yes;TrustServerCertificate="
        + (config.trustServerCertificate ? L"yes;" : L"no;");

    CheckOdbc(SQLAllocHandle(SQL_HANDLE_DBC, environment_.GetHandle(), &handle_),
        SQL_HANDLE_ENV, environment_.GetHandle(), "SQLAllocHandle(DBC)");
    try
    {
        CheckOdbc(SQLSetConnectAttrW(handle_, SQL_ATTR_LOGIN_TIMEOUT,
            reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(5)), 0),
            SQL_HANDLE_DBC, handle_, "SQLSetConnectAttr(LOGIN_TIMEOUT)");

        // 현재 Windows 계정으로 연결한다. 연결 문자열은 로그에 남기지 않는다.
        CheckOdbc(SQLDriverConnectW(handle_, nullptr, connectionString.data(), SQL_NTS,
            nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT), SQL_HANDLE_DBC, handle_, "SQLDriverConnectW");
    }
    catch (...)
    {
        SQLFreeHandle(SQL_HANDLE_DBC, handle_);
        throw;
    }
}

DatabaseConnection::~DatabaseConnection()
{
    SQLDisconnect(handle_);
    SQLFreeHandle(SQL_HANDLE_DBC, handle_);
}

DatabaseCheckResult DatabaseConnection::CheckConnection()
{
    const OdbcStatement statement(handle_);
    const SQLHSTMT handle = statement.GetHandle();
    CheckOdbc(SQLSetStmtAttrW(handle, SQL_ATTR_QUERY_TIMEOUT,
        reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(5)), 0),
        SQL_HANDLE_STMT, handle, "SQLSetStmtAttr(QUERY_TIMEOUT)");

    SQLWCHAR query[] =
        L"SELECT CONVERT(nvarchar(128), SERVERPROPERTY('ServerName')), DB_NAME(), "
        L"CONVERT(nvarchar(128), SERVERPROPERTY('ProductVersion')), "
        L"CONVERT(nvarchar(40), CONNECTIONPROPERTY('net_transport')), "
        L"CONVERT(nvarchar(40), CONNECTIONPROPERTY('auth_scheme')), "
        L"CAST(1 AS int), N'한글 연결 확인';";
    CheckOdbc(SQLExecDirectW(handle, query, SQL_NTS), SQL_HANDLE_STMT, handle, "SQLExecDirectW(check)");
    CheckOdbc(SQLFetch(handle), SQL_HANDLE_STMT, handle, "SQLFetch(check)");

    DatabaseCheckResult result;
    result.serverName = ToUtf8(ReadCheckText(handle, 1));
    const wstring databaseName = ReadCheckText(handle, 2);
    result.databaseName = ToUtf8(databaseName);
    result.productVersion = ToUtf8(ReadCheckText(handle, 3));
    result.transport = ToUtf8(ReadCheckText(handle, 4));
    result.authentication = ToUtf8(ReadCheckText(handle, 5));

    SQLINTEGER checkValue = 0;
    SQLLEN bytes = 0;
    CheckOdbc(SQLGetData(handle, 6, SQL_C_SLONG, &checkValue, sizeof(checkValue), &bytes),
        SQL_HANDLE_STMT, handle, "SQLGetData(check value)");
    if (bytes != sizeof(checkValue) || checkValue != 1)
    {
        throw runtime_error("Connection check did not return the expected integer.");
    }
    if (ReadCheckText(handle, 7) != L"한글 연결 확인")
    {
        throw runtime_error("Unicode query result did not match.");
    }
    if (databaseName != databaseName_)
    {
        throw runtime_error("Connected database did not match the configured name: " + result.databaseName);
    }

    const SQLRETURN nextRow = SQLFetch(handle);
    if (nextRow != SQL_NO_DATA)
    {
        CheckOdbc(nextRow, SQL_HANDLE_STMT, handle, "SQLFetch(end)");
        throw runtime_error("Connection check returned unexpected extra rows.");
    }
    return result;
}
