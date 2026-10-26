#pragma once

#include "Common.h"
#include "SpatialIndex.h"

class CIniFile
{
public:
    explicit CIniFile(const fs::path& path);
    const string& Get(const string& section, const string& key) const;
    int GetInt(const string& section, const string& key, int low, int high) const;
    double GetNumber(const string& section, const string& key) const;
    bool Has(const string& section, const string& key) const
    {
        return m_values.count(section + "." + key) != 0;
    }

private:
    unordered_map<string, string> m_values;
};

int ParseInt(string_view text, int low, int high);
double ParseNumber(const string& text);

struct stGameServerDefinition
{
    string id;
    string name;
    string race;
    string address;
    int port = 0;
};

struct stServerConfig
{
    string id;
    string type;
    string race;
    string address;
    int port = 0;
    int workerCount = 0;
    int maxClientsPerWorker = 0;
    int maxPendingSendBytes = 0;
    int pollTimeoutMs = 5;
    string networkMode = "WSAPoll";
    int battleWorkerCount = 1;
    bool verifyParallel = false;
    bool useMemoryPool = false;
    int poolCapacity = 8192;
    int tickRate = 0;
    int mpRegenPerSecond = 0;
    bool pauseMpRegenWhileCloaked = true;
    int deathReentrySeconds = 0;
    int roundRestartSeconds = 0;
    int maxWorldPlayers = 0;
    int maxGamePlayers = 64;
    double snapshotRadius = 0;
    SpatialSearchMode spatialSearch = SpatialSearchMode::Linear;
    double spatialCellSize = 25;
    bool verifySpatialSearch = false;
    int replayFrameTicks = 1;
    double buildingDetectionRange = 10;
    // 로컬 Redis 연결. 랭킹 I/O는 World의 별도 워커에서만 실행한다.
    bool redisEnabled = false;
    string redisHost = "127.0.0.1";
    string redisPrefix = "portfolio";
    int redisPort = 6379;
    int redisTimeoutMs = 200;
    int redisFlushMs = 1000;
    int redisRetentionSeconds = 604800;
    int redisQueryCapacity = 64;
    int rankMaxPlayers = 10000;
    string databaseRealm;
    int databaseWorkerCount = 2;
    int databaseQueueCapacity = 256;
    int sessionLeaseSeconds = 15;
    bool replayEnabled = true;
    int replayQueueMB = 8;
    int replayFileMB = 128;
    int replaySnapshotSeconds = 1;
    int metricsSeconds = 5;
    fs::path replayDirectory;
    fs::path configFile;
    fs::path serverFile;
    fs::path stopFile;
    string worldAddress;
    int worldPort = 0;
    vector<stGameServerDefinition> gameServers;
    fs::path clusterKeyFile;
    fs::path unitFile;
    fs::path skillFile;
    fs::path mapFile;
    fs::path startPointFile;
};

struct stSpawnPoint
{
    string race;
    double x = 0;
    double y = 0;
    string building;
    double buildingX = 0;
    double buildingY = 0;
    double buildingRadius = 0;
    int buildingHP = 0;
    int buildingShield = 0;
};

struct stMapConfig
{
    int index = 0;
    double minX = 0;
    double maxX = 0;
    double minY = 0;
    double maxY = 0;
    array<stSpawnPoint, 3> spawns;
};

stServerConfig LoadServerConfig(const fs::path& path, const string& id);
vector<stGameServerDefinition> LoadGameServers(const fs::path& configPath);
stMapConfig LoadMapConfig(const fs::path& path, const fs::path& startPointFile);
