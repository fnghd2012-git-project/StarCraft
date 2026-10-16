#pragma once

#include "Common.h"

#include <windows.h>
#include <sql.h>
#include <sqlext.h>

struct DatabaseConfig
{
    wstring driver = L"ODBC Driver 17 for SQL Server";
    wstring server = L"tcp:127.0.0.1,1433";
    wstring database = L"project_db";
    bool trustServerCertificate = true;
};

DatabaseConfig LoadDatabaseConfig();

struct DatabaseCheckResult
{
    string serverName;
    string databaseName;
    string productVersion;
    string transport;
    string authentication;
};

class OdbcEnvironment final
{
public:
    OdbcEnvironment();
    ~OdbcEnvironment();

    OdbcEnvironment(const OdbcEnvironment&) = delete;
    OdbcEnvironment& operator=(const OdbcEnvironment&) = delete;

    SQLHENV GetHandle() const { return handle_; }

private:
    SQLHENV handle_ = SQL_NULL_HENV;
};

class DatabaseConnection final
{
public:
    explicit DatabaseConnection(const DatabaseConfig& config);
    ~DatabaseConnection();

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;

    DatabaseCheckResult CheckConnection();

private:
    // 연결 핸들을 해제한 뒤 환경 핸들이 소멸하도록 직접 소유한다.
    OdbcEnvironment environment_;
    SQLHDBC handle_ = SQL_NULL_HDBC;
    wstring databaseName_;
};
