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
    const int bytes =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes == 0)
    {
        throw runtime_error("UTF-8 conversion failed.");
    }

    string result(bytes, '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), bytes, nullptr, nullptr) == 0)
    {
        throw runtime_error("UTF-8 conversion failed.");
    }
    return result;
}

wstring ReadEnvironment(const wchar_t* p_name, const wstring& defaultValue)
{
    array<wchar_t, 512> value{};
    const DWORD length = GetEnvironmentVariableW(p_name, value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
    {
        return defaultValue;
    }
    if (length == 0 || length >= value.size())
    {
        throw runtime_error("Invalid environment setting: " + ToUtf8(p_name));
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
        const SQLRETURN status = SQLGetDiagRecW(handleType,
                                                handle,
                                                static_cast<SQLSMALLINT>(record),
                                                state,
                                                &nativeError,
                                                message.data(),
                                                static_cast<SQLSMALLINT>(message.size()),
                                                &messageLength);
        if (status == SQL_NO_DATA)
        {
            break;
        }
        if (!SQL_SUCCEEDED(status))
        {
            result += "\nODBC diagnostic retrieval failed.";
            break;
        }

        result += "\nSQLSTATE=" + ToUtf8(state) + " NativeError=" + to_string(nativeError) + " " +
                  ToUtf8(message.data());
        if (status == SQL_SUCCESS_WITH_INFO)
        {
            result += " [diagnostic truncated]";
        }
    }
    return result;
}

void CheckOdbc(SQLRETURN result, SQLSMALLINT handleType, SQLHANDLE handle, const char* p_operation)
{
    if (!SQL_SUCCEEDED(result))
    {
        throw runtime_error(string(p_operation) + " failed." + ReadDiagnostics(handleType, handle));
    }
    if (result == SQL_SUCCESS_WITH_INFO)
    {
        cerr << "[ODBC INFO] " << p_operation << ReadDiagnostics(handleType, handle) << '\n';
    }
}

wstring ReadCheckText(SQLHSTMT statement, SQLUSMALLINT column)
{
    // 연결 점검 쿼리의 문자열은 모두 nvarchar(128) 이하다.
    array<SQLWCHAR, 256> value{};
    SQLLEN bytes = 0;
    CheckOdbc(SQLGetData(statement, column, SQL_C_WCHAR, value.data(), sizeof(value), &bytes),
              SQL_HANDLE_STMT,
              statement,
              "SQLGetData(text)");
    if (bytes < 0 || bytes >= static_cast<SQLLEN>(sizeof(value)))
    {
        throw runtime_error("Connection check returned NULL or truncated text.");
    }
    return wstring(value.data(), static_cast<size_t>(bytes) / sizeof(SQLWCHAR));
}
} // namespace

COdbcStatement::COdbcStatement(SQLHDBC connection)
{
    CheckOdbc(SQLAllocHandle(SQL_HANDLE_STMT, connection, &m_handle),
              SQL_HANDLE_DBC,
              connection,
              "SQLAllocHandle(STMT)");
}

COdbcStatement::~COdbcStatement()
{
    SQLFreeHandle(SQL_HANDLE_STMT, m_handle);
}

stDatabaseConfig LoadDatabaseConfig()
{
    stDatabaseConfig config;
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

COdbcEnvironment::COdbcEnvironment()
{
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_handle)))
    {
        throw runtime_error("SQLAllocHandle(ENV) failed.");
    }

    try
    {
        CheckOdbc(SQLSetEnvAttr(m_handle,
                                SQL_ATTR_ODBC_VERSION,
                                reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(SQL_OV_ODBC3)),
                                0),
                  SQL_HANDLE_ENV,
                  m_handle,
                  "SQLSetEnvAttr(ODBC3)");
    }
    catch (...)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, m_handle);
        throw;
    }
}

COdbcEnvironment::~COdbcEnvironment()
{
    SQLFreeHandle(SQL_HANDLE_ENV, m_handle);
}

CDatabaseConnection::CDatabaseConnection(const stDatabaseConfig& config) : m_databaseName(config.database)
{
    wstring connectionString = L"DRIVER=" + QuoteConnectionValue(config.driver) + L";SERVER=" +
                               QuoteConnectionValue(config.server) + L";DATABASE=" +
                               QuoteConnectionValue(config.database) +
                               L";Trusted_Connection=yes;Encrypt=yes;TrustServerCertificate=" +
                               (config.trustServerCertificate ? L"yes;" : L"no;");

    CheckOdbc(SQLAllocHandle(SQL_HANDLE_DBC, m_environment.GetHandle(), &m_handle),
              SQL_HANDLE_ENV,
              m_environment.GetHandle(),
              "SQLAllocHandle(DBC)");
    try
    {
        CheckOdbc(SQLSetConnectAttrW(
                      m_handle,
                      SQL_ATTR_LOGIN_TIMEOUT,
                      reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(config.loginTimeoutSeconds)),
                      0),
                  SQL_HANDLE_DBC,
                  m_handle,
                  "SQLSetConnectAttr(LOGIN_TIMEOUT)");

        // 현재 Windows 계정으로 연결한다. 연결 문자열은 로그에 남기지 않는다.
        CheckOdbc(SQLDriverConnectW(m_handle,
                                    nullptr,
                                    connectionString.data(),
                                    SQL_NTS,
                                    nullptr,
                                    0,
                                    nullptr,
                                    SQL_DRIVER_NOPROMPT),
                  SQL_HANDLE_DBC,
                  m_handle,
                  "SQLDriverConnectW");
    }
    catch (...)
    {
        SQLFreeHandle(SQL_HANDLE_DBC, m_handle);
        throw;
    }
}

CDatabaseConnection::~CDatabaseConnection()
{
    SQLDisconnect(m_handle);
    SQLFreeHandle(SQL_HANDLE_DBC, m_handle);
}

array<string, 5> CDatabaseConnection::ExecuteSession(const vector<string>& parameters)
{
    if (parameters.size() != 16)
    {
        throw runtime_error("Invalid session parameter count");
    }
    return ExecuteCommand(
        L"EXEC dbo.PortfolioSessionCommand ?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?", parameters, 256);
}

void CDatabaseConnection::SaveMatch(const string& xml)
{
    ExecuteCommand(L"EXEC dbo.PortfolioSaveMatch ?", {xml}, 16 * 1024 * 1024);
}

array<string, 5> CDatabaseConnection::ExecuteCommand(wstring query,
                                                     const vector<string>& parameters,
                                                     size_t maxLength)
{
    const COdbcStatement statement(m_handle);
    const auto handle = statement.GetHandle();
    CheckOdbc(SQLSetStmtAttrW(handle, SQL_ATTR_QUERY_TIMEOUT, reinterpret_cast<SQLPOINTER>(2), 0),
              SQL_HANDLE_STMT,
              handle,
              "SQLSetStmtAttr(session timeout)");
    CheckOdbc(SQLPrepareW(handle, query.data(), SQL_NTS), SQL_HANDLE_STMT, handle, "SQLPrepare(session)");
    vector<wstring> values;
    values.reserve(parameters.size());
    vector<SQLLEN> lengths(parameters.size(), SQL_NTS);
    // 문자열 결합 SQL을 만들지 않는다. 계정 이름도 Unicode 매개변수로 바인딩한다.
    for (const auto& parameter : parameters)
    {
        if (parameter.size() > maxLength)
        {
            throw runtime_error("Session parameter too long");
        }
        const int size = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, parameter.data(), static_cast<int>(parameter.size()), nullptr, 0);
        if (!parameter.empty() && size <= 0)
        {
            throw runtime_error("Invalid UTF-8 session parameter");
        }
        wstring value(size, L'\0');
        if (size)
        {
            MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                parameter.data(),
                                static_cast<int>(parameter.size()),
                                value.data(),
                                size);
        }
        values.push_back(move(value));
    }
    for (size_t i = 0; i < values.size(); ++i)
    {
        CheckOdbc(SQLBindParameter(handle,
                                   static_cast<SQLUSMALLINT>(i + 1),
                                   SQL_PARAM_INPUT,
                                   SQL_C_WCHAR,
                                   values[i].size() > 4000 ? SQL_WLONGVARCHAR : SQL_WVARCHAR,
                                   max<size_t>(1, values[i].size()),
                                   0,
                                   values[i].data(),
                                   static_cast<SQLLEN>((values[i].size() + 1) * sizeof(wchar_t)),
                                   &lengths[i]),
                  SQL_HANDLE_STMT,
                  handle,
                  "SQLBindParameter(session)");
    }
    CheckOdbc(SQLExecute(handle), SQL_HANDLE_STMT, handle, "SQLExecute(session)");
    CheckOdbc(SQLFetch(handle), SQL_HANDLE_STMT, handle, "SQLFetch(session)");
    array<string, 5> result;
    for (size_t i = 0; i < result.size(); ++i)
    {
        result[i] = ToUtf8(ReadCheckText(handle, static_cast<SQLUSMALLINT>(i + 1)));
    }
    return result;
}

stDatabaseCheckResult CDatabaseConnection::CheckConnection()
{
    const COdbcStatement statement(m_handle);
    const SQLHSTMT handle = statement.GetHandle();
    CheckOdbc(SQLSetStmtAttrW(
                  handle, SQL_ATTR_QUERY_TIMEOUT, reinterpret_cast<SQLPOINTER>(static_cast<uintptr_t>(5)), 0),
              SQL_HANDLE_STMT,
              handle,
              "SQLSetStmtAttr(QUERY_TIMEOUT)");

    SQLWCHAR query[] = L"SELECT CONVERT(nvarchar(128), SERVERPROPERTY('ServerName')), DB_NAME(), "
                       L"CONVERT(nvarchar(128), SERVERPROPERTY('ProductVersion')), "
                       L"CONVERT(nvarchar(40), CONNECTIONPROPERTY('net_transport')), "
                       L"CONVERT(nvarchar(40), CONNECTIONPROPERTY('auth_scheme')), "
                       L"CAST(1 AS int), N'한글 연결 확인';";
    CheckOdbc(SQLExecDirectW(handle, query, SQL_NTS), SQL_HANDLE_STMT, handle, "SQLExecDirectW(check)");
    CheckOdbc(SQLFetch(handle), SQL_HANDLE_STMT, handle, "SQLFetch(check)");

    stDatabaseCheckResult result;
    result.serverName = ToUtf8(ReadCheckText(handle, 1));
    const wstring databaseName = ReadCheckText(handle, 2);
    result.databaseName = ToUtf8(databaseName);
    result.productVersion = ToUtf8(ReadCheckText(handle, 3));
    result.transport = ToUtf8(ReadCheckText(handle, 4));
    result.authentication = ToUtf8(ReadCheckText(handle, 5));

    SQLINTEGER checkValue = 0;
    SQLLEN bytes = 0;
    CheckOdbc(SQLGetData(handle, 6, SQL_C_SLONG, &checkValue, sizeof(checkValue), &bytes),
              SQL_HANDLE_STMT,
              handle,
              "SQLGetData(check value)");
    if (bytes != sizeof(checkValue) || checkValue != 1)
    {
        throw runtime_error("Connection check did not return the expected integer.");
    }
    if (ReadCheckText(handle, 7) != L"한글 연결 확인")
    {
        throw runtime_error("Unicode query result did not match.");
    }
    if (databaseName != m_databaseName)
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
