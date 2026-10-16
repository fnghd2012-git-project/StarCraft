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
}

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

IniFile::IniFile(const fs::path& path)
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
        if (!values_.emplace(key, Trim(line.substr(equals + 1))).second)
        {
            throw runtime_error("Duplicate INI key: " + key);
        }
    }
    if (input.bad())
    {
        throw runtime_error("Failed while reading INI");
    }
}

const string& IniFile::Get(const string& section, const string& key) const
{
    const auto found = values_.find(section + "." + key);
    if (found == values_.end() || found->second.empty())
    {
        throw runtime_error("Missing INI value: " + section + "." + key);
    }
    return found->second;
}

int IniFile::GetInt(const string& section, const string& key, int low, int high) const
{
    return ParseInt(Get(section, key), low, high);
}

double IniFile::GetNumber(const string& section, const string& key) const
{
    return ParseNumber(Get(section, key));
}

ServerConfig LoadServerConfig(const fs::path& path, const string& id)
{
    // 같은 실행 파일을 설정만 바꾸어 A/B/C/World 역할로 띄운다.
    if (id != "A" && id != "B" && id != "C" && id != "World")
    {
        throw runtime_error("Server id must be A, B, C or World");
    }
    const IniFile ini(path);
    const string section = id == "World" ? id : "Game" + id;
    ServerConfig config;
    config.id = ini.Get(section, "ServerId");
    config.type = ini.Get(section, "ServerType");
    config.race = ini.Get(section, "Race");
    config.address = ini.Get(section, "Address");
    config.port = ini.GetInt(section, "Port", 1, 65535);
    const string expectedRace = id == "A" ? "Terran" : id == "B" ? "Protoss" : id == "C" ? "Zerg" : "None";
    if (config.id != id || config.race != expectedRace || config.type != (id == "World" ? "World" : "Game"))
    {
        throw runtime_error("Server identity does not match section: " + section);
    }
    config.workerCount = ini.GetInt("Network", "WorkerCount", 1, 32);
    config.maxClientsPerWorker = ini.GetInt("Network", "MaxClientsPerWorker", 1, 1024);
    config.maxPendingSendBytes = ini.GetInt("Network", "MaxPendingSendBytes", 4096, 1048576);
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
    config.unitFile = directory / Utf8Path(ini.Get("Data", "UnitFile"));
    config.skillFile = directory / Utf8Path(ini.Get("Data", "SkillFile"));
    config.mapFile = directory / Utf8Path(ini.Get("Data", "MapFile"));
    config.startPointFile = directory / Utf8Path(ini.Get("Data", "StartPointFile"));
    return config;
}

MapConfig LoadMapConfig(const fs::path& path, const fs::path& startPointFile)
{
    // 맵 외곽/건물은 INI, 진영별 시작 좌표는 StartPoint.csv에서 읽는다.
    const IniFile ini(path);
    MapConfig map;
    map.index = ini.GetInt("Map", "Index", 1, 1000000);
    map.minX = ini.GetNumber("Map", "MinX");
    map.maxX = ini.GetNumber("Map", "MaxX");
    map.minY = ini.GetNumber("Map", "MinY");
    map.maxY = ini.GetNumber("Map", "MaxY");
    if (map.minX >= map.maxX || map.minY >= map.maxY ||
        map.minX < 0 || map.maxX > 1000000 || map.minY < 0 || map.maxY > 1000000)
    {
        throw runtime_error("Invalid map bounds");
    }
    const array<string, 3> races = { "Terran", "Protoss", "Zerg" };
    const array<string, 3> buildings = { "CommandCenter", "Nexus", "Hive" };
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
            !mapRaces.insert(to_string(mapIndex) + ":" + race).second || find(races.begin(), races.end(), race) == races.end())
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
        if (spawn.building != buildings[i] || spawn.buildingRadius <= 0 ||
            spawn.x < map.minX || spawn.x > map.maxX || spawn.y < map.minY || spawn.y > map.maxY ||
            spawn.buildingX - spawn.buildingRadius < map.minX || spawn.buildingX + spawn.buildingRadius > map.maxX ||
            spawn.buildingY - spawn.buildingRadius < map.minY || spawn.buildingY + spawn.buildingRadius > map.maxY ||
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
