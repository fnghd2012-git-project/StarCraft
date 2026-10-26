#pragma once

#include "Common.h"

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

struct stDatabaseConfig
{
    wstring driver = L"ODBC Driver 17 for SQL Server";
    wstring server = L"tcp:127.0.0.1,1433";
    wstring database = L"project_db";
    bool trustServerCertificate = true;
    int loginTimeoutSeconds = 5;
};

stDatabaseConfig LoadDatabaseConfig();

struct stDatabaseCheckResult
{
    string serverName;
    string databaseName;
    string productVersion;
    string transport;
    string authentication;
};

class COdbcEnvironment final
{
public:
    COdbcEnvironment();
    ~COdbcEnvironment();

    COdbcEnvironment(const COdbcEnvironment&) = delete;
    COdbcEnvironment& operator=(const COdbcEnvironment&) = delete;

    SQLHENV GetHandle() const
    {
        return m_handle;
    }

private:
    SQLHENV m_handle = SQL_NULL_HENV;
};

// SQL 문장 핸들의 수명 관리. 생성/해제 구현은 Database.cpp에서 확인한다.
class COdbcStatement final
{
public:
    explicit COdbcStatement(SQLHDBC connection);

    ~COdbcStatement();

    COdbcStatement(const COdbcStatement&) = delete;
    COdbcStatement& operator=(const COdbcStatement&) = delete;

    SQLHSTMT GetHandle() const
    {
        return m_handle;
    }

private:
    SQLHSTMT m_handle = SQL_NULL_HSTMT;
};

class CDatabaseConnection final
{
public:
    explicit CDatabaseConnection(const stDatabaseConfig& config);
    ~CDatabaseConnection();

    CDatabaseConnection(const CDatabaseConnection&) = delete;
    CDatabaseConnection& operator=(const CDatabaseConnection&) = delete;

    stDatabaseCheckResult CheckConnection();
    array<string, 5> ExecuteSession(const vector<string>& parameters);
    void SaveMatch(const string& xml);

private:
    array<string, 5> ExecuteCommand(wstring query, const vector<string>& parameters, size_t maxLength);
    // 연결 핸들을 해제한 뒤 환경 핸들이 소멸하도록 직접 소유한다.
    COdbcEnvironment m_environment;
    SQLHDBC m_handle = SQL_NULL_HDBC;
    wstring m_databaseName;
};
