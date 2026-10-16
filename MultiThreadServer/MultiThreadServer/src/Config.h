#pragma once

#include "Common.h"

class IniFile
{
public:
    explicit IniFile(const fs::path& path);
    const string& Get(const string& section, const string& key) const;
    int GetInt(const string& section, const string& key, int low, int high) const;
    double GetNumber(const string& section, const string& key) const;

private:
    unordered_map<string, string> values_;
};

int ParseInt(string_view text, int low, int high);
double ParseNumber(const string& text);

struct ServerConfig
{
    string id;
    string type;
    string race;
    string address;
    int port = 0;
    int workerCount = 0;
    int maxClientsPerWorker = 0;
    int maxPendingSendBytes = 0;
    int tickRate = 0;
    int mpRegenPerSecond = 0;
    bool pauseMpRegenWhileCloaked = true;
    int deathReentrySeconds = 0;
    int roundRestartSeconds = 0;
    fs::path unitFile;
    fs::path skillFile;
    fs::path mapFile;
    fs::path startPointFile;
};

struct SpawnPoint
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

struct MapConfig
{
    int index = 0;
    double minX = 0;
    double maxX = 0;
    double minY = 0;
    double maxY = 0;
    array<SpawnPoint, 3> spawns;
};

ServerConfig LoadServerConfig(const fs::path& path, const string& id);
MapConfig LoadMapConfig(const fs::path& path, const fs::path& startPointFile);
