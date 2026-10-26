#pragma once

#include "CsvTable.h"

struct stUnitDefinition
{
    int index = 0;
    string race;
    string name;
    int hp = 0;
    int shield = 0;
    int mp = 0;
    int initialMP = 0;
    int armor = 0;
    int damage = 0;
    int attackCount = 0;
    int attackIntervalMs = 0;
    double range = 0;
    double speed = 0;
    string attackType;
    string damageType;
    string size;
    bool biological = false;
    bool mechanical = false;
    double sight = 0;
    double detection = 0;
    array<int, 5> skills{};
};

struct stSkillDefinition
{
    int index = 0;
    string name;
    string type;
    string castType;
    string targetType;
    string filter;
    string stackRule;
    int mpCost = 0;
    int hpCost = 0;
    int mpPerSecond = 0;
    int cooldownMs = 0;
    int castTimeMs = 0;
    int durationMs = 0;
    int intervalMs = 0;
    int power = 0;
    int movePercent = 100;
    int attackPercent = 100;
    int minHP = 0;
    double range = 0;
    double radius = 0;
    bool buildings = false;
    bool allies = false;
    bool enemies = false;
};

// 시작할 때만 읽는다. 실행 중인 워커에는 수정 권한을 주지 않는다.
struct stGameData
{
    stCsvTable units;
    stCsvTable skills;
    vector<stUnitDefinition> definitions;
    unordered_map<int, stSkillDefinition> skillDefinitions;
};

stGameData LoadGameData(const stServerConfig& config);
