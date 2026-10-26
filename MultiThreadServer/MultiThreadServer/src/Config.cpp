#include "Config.h"
#include "CsvTable.h"

namespace
{
fs::path Utf8Path(const string& value)
{
    // INI는 UTF-8이다. Windows 기본 코드 페이지와 관계없이 한글 경로를 보존한다.
    return fs::path(u8string(value.begin(), value.end()));
}

string Trim(string text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace

int ParseInt(string_view text, int low, int high)
{
    // "100abc" 같은 값도 거부한다. 변환 성공뿐 아니라 문자열 전체 소비 여부를 확인한다.
    int value = 0;
    const auto result = from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != errc{} || result.ptr != text.data() + text.size() || value < low || value > high)
    {
        throw runtime_error("Invalid integer: " + string(text));
    }
    return value;
}

double ParseNumber(const string& text)
{
    double value = 0;
    const auto result = from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != errc{} || result.ptr != text.data() + text.size() || !isfinite(value))
    {
        throw runtime_error("Invalid number: " + text);
    }
    return value;
}

CIniFile::CIniFile(const fs::path& path)
{
    // 설정 오류를 조용히 기본값으로 바꾸지 않고 시작 시 알려준다.
    ifstream input(path);
    if (!input)
    {
        throw runtime_error("Cannot open INI: " + path.string());
    }
    string section;
    string line;
    int lineNumber = 0;
    while (getline(input, line))
    {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            line.erase(0, 3);
        }
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            section = Trim(line.substr(1, line.size() - 2));
            if (!section.empty())
            {
                continue;
            }
        }
        const size_t equals = line.find('=');
        if (section.empty() || equals == string::npos || Trim(line.substr(0, equals)).empty())
        {
            throw runtime_error("Invalid INI line " + to_string(lineNumber));
        }
        const string key = section + "." + Trim(line.substr(0, equals));
        if (!m_values.emplace(key, Trim(line.substr(equals + 1))).second)
        {
            throw runtime_error("Duplicate INI key: " + key);
        }
    }
    if (input.bad())
    {
        throw runtime_error("Failed while reading INI");
    }
}

const string& CIniFile::Get(const string& section, const string& key) const
{
    const auto found = m_values.find(section + "." + key);
    if (found == m_values.end() || found->second.empty())
    {
        throw runtime_error("Missing INI value: " + section + "." + key);
    }
    return found->second;
}

int CIniFile::GetInt(const string& section, const string& key, int low, int high) const
{
    return ParseInt(Get(section, key), low, high);
}

double CIniFile::GetNumber(const string& section, const string& key) const
{
    return ParseNumber(Get(section, key));
}

vector<stGameServerDefinition> LoadGameServers(const fs::path& configPath)
{
    const CIniFile ini(configPath);
    const fs::path directory = fs::absolute(configPath).parent_path();
    const auto table =
        LoadCsvTable(directory / Utf8Path(ini.Get("Data", "ServerFile")), "Index,Name,Race,Address,Port");
    if (table.rows.empty() || table.rows.size() > 64)
    {
        throw runtime_error("GameServer.csv requires 1..64 servers");
    }
    vector<stGameServerDefinition> servers;
    unordered_set<string> ids;
    unordered_set<string> endpoints;
    endpoints.insert(ini.Get("World", "Address") + ":" + ini.Get("World", "Port"));
    for (size_t row = 0; row < table.rows.size(); ++row)
    {
        stGameServerDefinition server;
        server.id = table.Get(row, "Index");
        server.name = table.Get(row, "Name");
        server.race = table.Get(row, "Race");
        if (server.race == "테란")
        {
            server.race = "Terran";
        }
        else if (server.race == "프로토스")
        {
            server.race = "Protoss";
        }
        else if (server.race == "저그")
        {
            server.race = "Zerg";
        }
        server.address = table.Get(row, "Address");
        server.port = ParseInt(table.Get(row, "Port"), 1, 65535);
        // SQL의 서버 키와 충돌하지 않도록 대소문자만 다른 ID와 World 변형도 예약한다.
        string serverKey = server.id;
        for (char& ch : serverKey)
        {
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        if (server.id.empty() || server.id.size() > 16 || serverKey == "world" ||
            !ids.insert(serverKey).second || server.name.empty() || server.name.size() > 64 ||
            (server.race != "Terran" && server.race != "Protoss" && server.race != "Zerg") ||
            !endpoints.insert(server.address + ":" + to_string(server.port)).second)
        {
            throw runtime_error("Invalid or duplicate game server definition");
        }
        for (const unsigned char ch : server.id)
        {
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                  ch == '_' || ch == '-'))
            {
                throw runtime_error("Server Index allows letters, digits, underscore and hyphen");
            }
        }
        servers.push_back(move(server));
    }
    return servers;
}

stServerConfig LoadServerConfig(const fs::path& path, const string& id)
{
    // 서버 ID는 CSV의 키일 뿐이다. 종족/서버 수/포트는 행 데이터로 결정한다.
    const CIniFile ini(path);
    stServerConfig config;
    config.gameServers = LoadGameServers(path);
    config.id = id;
    if (id == "World")
    {
        config.type = "World";
        config.race = "None";
        config.address = ini.Get("World", "Address");
        config.port = ini.GetInt("World", "Port", 1, 65535);
    }
    else
    {
        bool found = false;
        for (const auto& server : config.gameServers)
        {
            if (server.id != id)
            {
                continue;
            }
            config.type = "Game";
            config.race = server.race;
            config.address = server.address;
            config.port = server.port;
            found = true;
            break;
        }
        if (!found)
        {
            throw runtime_error("Server Index not found in GameServer.csv: " + id);
        }
    }
    // 서버 프로세스 하나의 네트워크 스레드 수. IOCP/WSAPoll 모두 이 값을 사용한다.
    config.workerCount = ini.GetInt("Network", "WorkerCount", 1, 32);
    config.maxClientsPerWorker = ini.GetInt("Network", "MaxClientsPerWorker", 1, 1024);
    config.maxPendingSendBytes = ini.GetInt("Network", "MaxPendingSendBytes", 4096, 1048576);
    config.pollTimeoutMs = ini.GetInt("Network", "PollTimeoutMs", 1, 50);
    if (ini.Has("Network", "Mode"))
    {
        config.networkMode = ini.Get("Network", "Mode");
    }
    if (config.networkMode != "WSAPoll" && config.networkMode != "IOCP")
    {
        throw runtime_error("Network Mode must be WSAPoll or IOCP");
    }
    // 전투 계산은 1이면 메인에서, 2 이상이면 별도 전투 쓰레드(워커)에서 나눠 처리한다.
    if (ini.Has("Battle", "WorkerCount"))
    {
        config.battleWorkerCount = ini.GetInt("Battle", "WorkerCount", 1, 16);
    }
    if (ini.Has("Battle", "VerifyParallel"))
    {
        config.verifyParallel = ini.GetInt("Battle", "VerifyParallel", 0, 1) != 0;
    }
    if (ini.Has("Memory", "UsePool"))
    {
        config.useMemoryPool = ini.GetInt("Memory", "UsePool", 0, 1) != 0;
    }
    if (ini.Has("Memory", "PoolCapacity"))
    {
        config.poolCapacity = ini.GetInt("Memory", "PoolCapacity", 1, 65536);
    }
    config.databaseRealm = ini.Get("Database", "Realm");
    if (config.databaseRealm.empty() || config.databaseRealm.size() > 64 ||
        config.databaseRealm.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != string::npos)
    {
        throw runtime_error("Database Realm must be 1..64 lowercase letters, digits, underscore or hyphen");
    }
    // DB 쓰레드(워커) 수는 네트워크/전투 쓰레드(워커) 수와 별개다.
    config.databaseWorkerCount = ini.GetInt("Database", "WorkerCount", 1, 8);
    config.databaseQueueCapacity = ini.GetInt("Database", "QueueCapacity", 16, 4096);
    config.sessionLeaseSeconds = ini.GetInt("Database", "SessionLeaseSeconds", 10, 60);
    config.tickRate = ini.GetInt("Battle", "TickRate", 1, 1000);
    if (1000 % config.tickRate != 0)
    {
        throw runtime_error("TickRate must divide 1000 milliseconds");
    }
    config.mpRegenPerSecond = ini.GetInt("Battle", "MpRegenPerSecond", 0, 1000);
    config.pauseMpRegenWhileCloaked = ini.GetInt("Battle", "PauseMpRegenWhileCloaked", 0, 1) != 0;
    config.deathReentrySeconds = ini.GetInt("Battle", "DeathReentrySeconds", 0, 86400);
    config.roundRestartSeconds = ini.GetInt("Battle", "RoundRestartSeconds", 1, 86400);
    // 데이터 경로는 실행 위치가 아니라 Server.ini가 있는 폴더를 기준으로 해석한다.
    const fs::path directory = fs::absolute(path).parent_path();
    config.configFile = fs::absolute(path);
    if (ini.Has("Network", "StopFile"))
    {
        config.stopFile = directory / Utf8Path(ini.Get("Network", "StopFile"));
    }
    config.serverFile = directory / Utf8Path(ini.Get("Data", "ServerFile"));
    config.replayEnabled = ini.GetInt("Replay", "Enabled", 0, 1) != 0;
    config.replayDirectory = directory / Utf8Path(ini.Get("Replay", "Directory"));
    config.replayQueueMB = ini.GetInt("Replay", "QueueMB", 1, 32);
    config.replayFileMB = ini.GetInt("Replay", "MaxFileMB", 1, 1024);
    if (ini.Has("Replay", "FrameTicks"))
    {
        config.replayFrameTicks = ini.GetInt("Replay", "FrameTicks", 1, config.tickRate);
    }
    config.replaySnapshotSeconds = ini.GetInt("Replay", "SnapshotSeconds", 1, 10);
    if (ini.Has("Redis", "Enabled"))
    {
        config.redisEnabled = ini.GetInt("Redis", "Enabled", 0, 1) != 0;
    }
    if (config.redisEnabled)
    {
        config.redisHost = ini.Get("Redis", "Host");
        // 현재 포트폴리오는 개인 PC 전용이다. 외부 주소/인증 설정은 별도 확장 대상이다.
        if (config.redisHost != "127.0.0.1")
        {
            throw runtime_error("Redis Host must be 127.0.0.1");
        }
        config.redisPort = ini.GetInt("Redis", "Port", 1, 65535);
        config.redisPrefix = ini.Get("Redis", "KeyPrefix");
        if (config.redisPrefix.empty() || config.redisPrefix.size() > 32 ||
            config.redisPrefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != string::npos)
        {
            throw runtime_error("Invalid Redis KeyPrefix");
        }
        config.redisTimeoutMs = ini.GetInt("Redis", "TimeoutMs", 50, 2000);
        config.redisFlushMs = ini.GetInt("Redis", "FlushIntervalMs", 100, 5000);
        config.redisRetentionSeconds = ini.GetInt("Redis", "RetentionSeconds", 60, 2592000);
        config.redisQueryCapacity = ini.GetInt("Redis", "QueryCapacity", 1, 1024);
    }
    config.metricsSeconds = ini.GetInt("Metrics", "IntervalSeconds", 1, 60);
    config.maxWorldPlayers = ini.GetInt("Battle", "MaxWorldPlayers", 1, 2048);
    if (ini.Has("Battle", "MaxMatchParticipants"))
    {
        config.rankMaxPlayers = ini.GetInt("Battle", "MaxMatchParticipants", config.maxWorldPlayers, 100000);
    }
    if (ini.Has("Battle", "MaxGamePlayers"))
    {
        config.maxGamePlayers = ini.GetInt("Battle", "MaxGamePlayers", 1, 2048);
    }
    if (ini.Has("Server." + id, "MaxPlayers"))
    {
        config.maxGamePlayers = ini.GetInt("Server." + id, "MaxPlayers", 1, 2048);
    }
    if (ini.Has("Battle", "SnapshotRadius"))
    {
        config.snapshotRadius = ini.GetNumber("Battle", "SnapshotRadius");
    }
    if (ini.Has("Battle", "SpatialSearch"))
    {
        const auto& mode = ini.Get("Battle", "SpatialSearch");
        if (mode != "Linear" && mode != "Grid")
        {
            throw runtime_error("SpatialSearch must be Linear or Grid");
        }
        config.spatialSearch = mode == "Grid" ? SpatialSearchMode::Grid : SpatialSearchMode::Linear;
    }
    if (ini.Has("Battle", "SpatialCellSize"))
    {
        config.spatialCellSize = ini.GetNumber("Battle", "SpatialCellSize");
    }
    if (config.spatialCellSize < 1 || config.spatialCellSize > 1000000)
    {
        throw runtime_error("SpatialCellSize must be 1..1000000");
    }
    if (ini.Has("Battle", "VerifySpatialSearch"))
    {
        config.verifySpatialSearch = ini.GetInt("Battle", "VerifySpatialSearch", 0, 1) != 0;
    }
    if (config.snapshotRadius < 0 || config.snapshotRadius > 1000000 ||
        (config.maxWorldPlayers > 64 && config.snapshotRadius == 0))
    {
        throw runtime_error("World above 64 players requires a positive SnapshotRadius");
    }
    config.buildingDetectionRange = ini.GetNumber("Battle", "BuildingDetectionRange");
    if (config.buildingDetectionRange > 1000000)
    {
        throw runtime_error("Building detection range too large");
    }
    config.worldAddress = ini.Get("World", "Address");
    config.worldPort = ini.GetInt("World", "Port", 1, 65535);
    config.clusterKeyFile = directory / "Cluster.local.ini";
    config.unitFile = directory / Utf8Path(ini.Get("Data", "UnitFile"));
    config.skillFile = directory / Utf8Path(ini.Get("Data", "SkillFile"));
    config.mapFile = directory / Utf8Path(ini.Get("Data", "MapFile"));
    config.startPointFile = directory / Utf8Path(ini.Get("Data", "StartPointFile"));
    return config;
}

stMapConfig LoadMapConfig(const fs::path& path, const fs::path& startPointFile)
{
    // 맵 외곽/건물은 INI, 진영별 시작 좌표는 StartPoint.csv에서 읽는다.
    const CIniFile ini(path);
    stMapConfig map;
    map.index = ini.GetInt("Map", "Index", 1, 1000000);
    map.minX = ini.GetNumber("Map", "MinX");
    map.maxX = ini.GetNumber("Map", "MaxX");
    map.minY = ini.GetNumber("Map", "MinY");
    map.maxY = ini.GetNumber("Map", "MaxY");
    if (map.minX >= map.maxX || map.minY >= map.maxY || map.minX < 0 || map.maxX > 1000000 || map.minY < 0 ||
        map.maxY > 1000000)
    {
        throw runtime_error("Invalid map bounds");
    }
    const array<string, 3> races = {"Terran", "Protoss", "Zerg"};
    const array<string, 3> buildings = {"CommandCenter", "Nexus", "Hive"};
    const auto points = LoadCsvTable(startPointFile, "Index,MapIndex,Race,X,Y");
    unordered_set<int> pointIds;
    unordered_set<string> mapRaces;
    // 행 번호와 (맵 번호, 종족) 조합은 각각 유일해야 한다.
    for (size_t row = 0; row < points.rows.size(); ++row)
    {
        const int index = points.GetInt(row, "Index");
        const int mapIndex = points.GetInt(row, "MapIndex");
        const string& race = points.Get(row, "Race");
        if (index == 0 || mapIndex == 0 || !pointIds.insert(index).second ||
            !mapRaces.insert(to_string(mapIndex) + ":" + race).second ||
            find(races.begin(), races.end(), race) == races.end())
        {
            throw runtime_error("Invalid or duplicate start point");
        }
        // 모든 맵 좌표의 최소값은 0이다. 현재 맵이 아닌 행도 음수는 거부한다.
        points.GetNumber(row, "X");
        points.GetNumber(row, "Y");
    }
    for (size_t i = 0; i < races.size(); ++i)
    {
        auto& spawn = map.spawns[i];
        spawn.race = races[i];
        bool found = false;
        for (size_t row = 0; row < points.rows.size(); ++row)
        {
            if (points.GetInt(row, "MapIndex") == map.index && points.Get(row, "Race") == spawn.race)
            {
                spawn.x = points.GetNumber(row, "X");
                spawn.y = points.GetNumber(row, "Y");
                found = true;
                break;
            }
        }
        if (!found)
        {
            throw runtime_error("Missing start point: " + spawn.race);
        }
        spawn.building = ini.Get(spawn.race, "Building");
        spawn.buildingX = ini.GetNumber(spawn.race, "BuildingX");
        spawn.buildingY = ini.GetNumber(spawn.race, "BuildingY");
        spawn.buildingRadius = ini.GetNumber(spawn.race, "BuildingRadius");
        spawn.buildingHP = ini.GetInt(spawn.race, "BuildingHP", 1, 1000000);
        spawn.buildingShield = ini.GetInt(spawn.race, "BuildingShield", 0, 1000000);
        const double dx = spawn.x - spawn.buildingX;
        const double dy = spawn.y - spawn.buildingY;
        if (spawn.building != buildings[i] || spawn.buildingRadius <= 0 || spawn.x < map.minX ||
            spawn.x > map.maxX || spawn.y < map.minY || spawn.y > map.maxY ||
            spawn.buildingX - spawn.buildingRadius < map.minX ||
            spawn.buildingX + spawn.buildingRadius > map.maxX ||
            spawn.buildingY - spawn.buildingRadius < map.minY ||
            spawn.buildingY + spawn.buildingRadius > map.maxY ||
            dx * dx + dy * dy <= spawn.buildingRadius * spawn.buildingRadius)
        {
            throw runtime_error("Invalid spawn/building placement: " + spawn.race);
        }
    }
    // 시작 위치가 다른 진영 건물 안에 들어가는 경우도 막는다.
    for (const auto& spawn : map.spawns)
    {
        for (const auto& building : map.spawns)
        {
            const double dx = spawn.x - building.buildingX;
            const double dy = spawn.y - building.buildingY;
            if (dx * dx + dy * dy <= building.buildingRadius * building.buildingRadius)
            {
                throw runtime_error("Start point overlaps a building: " + spawn.race);
            }
        }
    }
    return map;
}
