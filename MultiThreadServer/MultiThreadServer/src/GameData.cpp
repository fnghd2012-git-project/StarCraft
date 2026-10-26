#include "GameData.h"

namespace
{
void RequireChoice(const string& value, const vector<string>& choices)
{
    if (find(choices.begin(), choices.end(), value) == choices.end())
    {
        throw runtime_error("Unknown data value: " + value);
    }
}
} // namespace

stGameData LoadGameData(const stServerConfig& config)
{
    // 데이터는 네트워크 쓰레드(워커)를 만들기 전에 전부 검증한다. 잘못된 상태로 접속을 받지 않는다.
    stGameData data;
    data.units =
        LoadCsvTable(config.unitFile,
                     "Race,UnitIndex,Name,AttackRange,HP,MP,InitialMP,Shield,Armor,AttackDamage,AttackCount,"
                     "AttackIntervalMs,MoveSpeed,AttackType,DamageType,UnitSize,IsBiological,IsMechanical,"
                     "SightRange,DetectionRange,Skill1,Skill2,Skill3,Skill4,Skill5");
    data.skills = LoadCsvTable(
        config.skillFile,
        "Index,SkillIndex,Name,SkillType,CastType,TargetType,TargetFilter,MpCost,HpCost,MpPerSecond,"
        "CooldownMs,CastTimeMs,DurationMs,TickIntervalMs,CastRange,AreaRadius,Power,MoveSpeedPercent,"
        "AttackSpeedPercent,MinTargetHP,AffectsBuildings,AffectsAllies,AffectsEnemies,StackRule");
    unordered_set<int> skillIds;
    unordered_set<int> rowIds;
    // Index는 행 관리 번호, SkillIndex는 유닛에서 참조하는 번호다. 서로 같을 필요는 없다.
    for (size_t row = 0; row < data.skills.rows.size(); ++row)
    {
        const auto& table = data.skills;
        const int id = table.GetInt(row, "SkillIndex");
        const int index = table.GetInt(row, "Index");
        if (id == 0 || index == 0 || !skillIds.insert(id).second || !rowIds.insert(index).second)
        {
            throw runtime_error("Invalid or duplicate SkillIndex/Index");
        }
        for (const string column : {"MpCost",
                                    "HpCost",
                                    "MpPerSecond",
                                    "CooldownMs",
                                    "CastTimeMs",
                                    "DurationMs",
                                    "TickIntervalMs",
                                    "Power",
                                    "MoveSpeedPercent",
                                    "AttackSpeedPercent",
                                    "MinTargetHP"})
        {
            table.GetInt(row, column);
        }
        table.GetNumber(row, "CastRange");
        table.GetNumber(row, "AreaRadius");
        for (const string column : {"AffectsBuildings", "AffectsAllies", "AffectsEnemies"})
        {
            ParseInt(table.Get(row, column), 0, 1);
        }
        RequireChoice(table.Get(row, "SkillType"),
                      {"Stim",
                       "Cloak",
                       "Lockdown",
                       "Heal",
                       "Cleanse",
                       "Blind",
                       "Storm",
                       "DarkSwarm",
                       "Plague",
                       "Burrow"});
        RequireChoice(table.Get(row, "CastType"), {"Instant", "Toggle", "Channel"});
        RequireChoice(table.Get(row, "TargetType"), {"Self", "Unit", "Point"});
        RequireChoice(table.Get(row, "TargetFilter"),
                      {"AnyUnit", "Mechanical", "BiologicalGround", "Ground"});
        RequireChoice(table.Get(row, "StackRule"), {"Refresh", "Unique"});
        const int duration = table.GetInt(row, "DurationMs");
        const int interval = table.GetInt(row, "TickIntervalMs");
        if ((table.Get(row, "CastType") == "Channel" && interval == 0) ||
            (duration > 0 && interval > 0 && duration % interval != 0))
        {
            throw runtime_error("Invalid skill tick interval");
        }
    }
    unordered_set<int> unitIds;
    // 기본 수치와 공격 불가 유닛의 0 값, 스킬 연결을 확인한다.
    for (size_t row = 0; row < data.units.rows.size(); ++row)
    {
        const auto& table = data.units;
        const int id = table.GetInt(row, "UnitIndex");
        if (id == 0 || !unitIds.insert(id).second)
        {
            throw runtime_error("Invalid or duplicate UnitIndex");
        }
        RequireChoice(table.Get(row, "Race"), {"Terran", "Protoss", "Zerg"});
        RequireChoice(table.Get(row, "AttackType"), {"None", "Melee", "Ranged"});
        RequireChoice(table.Get(row, "DamageType"), {"None", "Normal", "Concussive", "Explosive"});
        RequireChoice(table.Get(row, "UnitSize"), {"Small", "Medium", "Large"});
        for (const string column :
             {"HP", "MP", "InitialMP", "Shield", "Armor", "AttackDamage", "AttackCount", "AttackIntervalMs"})
        {
            table.GetInt(row, column);
        }
        for (const string column : {"AttackRange", "MoveSpeed", "SightRange", "DetectionRange"})
        {
            table.GetNumber(row, column);
        }
        ParseInt(table.Get(row, "IsBiological"), 0, 1);
        ParseInt(table.Get(row, "IsMechanical"), 0, 1);
        if (table.GetInt(row, "HP") == 0 || table.GetInt(row, "InitialMP") > table.GetInt(row, "MP") ||
            table.GetNumber(row, "MoveSpeed") == 0)
        {
            throw runtime_error("Invalid unit HP/MP/speed");
        }
        const bool noAttack = table.Get(row, "AttackType") == "None";
        if (noAttack != (table.Get(row, "DamageType") == "None") ||
            noAttack != (table.GetInt(row, "AttackDamage") == 0) ||
            noAttack != (table.GetInt(row, "AttackCount") == 0) ||
            noAttack != (table.GetInt(row, "AttackIntervalMs") == 0) ||
            noAttack != (table.GetNumber(row, "AttackRange") == 0))
        {
            throw runtime_error("Inconsistent unit attack fields");
        }
        unordered_set<int> slots;
        // 0은 빈 슬롯이다. 같은 유닛에 동일 스킬을 여러 번 넣는 것은 설정 실수로 처리한다.
        for (int slot = 1; slot <= 5; ++slot)
        {
            const int skill = table.GetInt(row, "Skill" + to_string(slot));
            if (skill != 0 && (skillIds.count(skill) == 0 || !slots.insert(skill).second))
            {
                throw runtime_error("Missing or duplicate unit skill reference");
            }
        }
    }
    for (size_t row = 0; row < data.units.rows.size(); ++row)
    {
        const auto& table = data.units;
        stUnitDefinition unit;
        unit.index = table.GetInt(row, "UnitIndex");
        unit.race = table.Get(row, "Race");
        unit.name = table.Get(row, "Name");
        if (unit.name.size() > 64)
        {
            throw runtime_error("Unit Name exceeds 64 UTF-8 bytes");
        }
        unit.hp = table.GetInt(row, "HP");
        unit.shield = table.GetInt(row, "Shield");
        unit.mp = table.GetInt(row, "MP");
        unit.initialMP = table.GetInt(row, "InitialMP");
        unit.armor = table.GetInt(row, "Armor");
        unit.damage = table.GetInt(row, "AttackDamage");
        unit.attackCount = table.GetInt(row, "AttackCount");
        unit.attackIntervalMs = table.GetInt(row, "AttackIntervalMs");
        unit.range = table.GetNumber(row, "AttackRange");
        unit.speed = table.GetNumber(row, "MoveSpeed");
        unit.attackType = table.Get(row, "AttackType");
        unit.damageType = table.Get(row, "DamageType");
        unit.size = table.Get(row, "UnitSize");
        unit.biological = table.GetInt(row, "IsBiological") != 0;
        unit.mechanical = table.GetInt(row, "IsMechanical") != 0;
        unit.sight = table.GetNumber(row, "SightRange");
        unit.detection = table.GetNumber(row, "DetectionRange");
        for (int slot = 0; slot < 5; ++slot)
        {
            unit.skills[slot] = table.GetInt(row, "Skill" + to_string(slot + 1));
        }
        if (unit.attackCount > 16 || unit.speed > 100 || unit.hp > 1000000 || unit.shield > 1000000 ||
            unit.mp > 1000000 || unit.damage > 1000000)
        {
            throw runtime_error("Unit combat values exceed supported limits");
        }
        data.definitions.push_back(move(unit));
    }
    for (size_t row = 0; row < data.skills.rows.size(); ++row)
    {
        const auto& table = data.skills;
        stSkillDefinition skill;
        skill.index = table.GetInt(row, "SkillIndex");
        skill.name = table.Get(row, "Name");
        skill.type = table.Get(row, "SkillType");
        skill.castType = table.Get(row, "CastType");
        skill.targetType = table.Get(row, "TargetType");
        skill.filter = table.Get(row, "TargetFilter");
        skill.stackRule = table.Get(row, "StackRule");
        skill.mpCost = table.GetInt(row, "MpCost");
        skill.hpCost = table.GetInt(row, "HpCost");
        skill.mpPerSecond = table.GetInt(row, "MpPerSecond");
        skill.cooldownMs = table.GetInt(row, "CooldownMs");
        skill.castTimeMs = table.GetInt(row, "CastTimeMs");
        skill.durationMs = table.GetInt(row, "DurationMs");
        skill.intervalMs = table.GetInt(row, "TickIntervalMs");
        skill.power = table.GetInt(row, "Power");
        skill.movePercent = table.GetInt(row, "MoveSpeedPercent");
        skill.attackPercent = table.GetInt(row, "AttackSpeedPercent");
        skill.minHP = table.GetInt(row, "MinTargetHP");
        skill.range = table.GetNumber(row, "CastRange");
        skill.radius = table.GetNumber(row, "AreaRadius");
        skill.buildings = table.GetInt(row, "AffectsBuildings") != 0;
        skill.allies = table.GetInt(row, "AffectsAllies") != 0;
        skill.enemies = table.GetInt(row, "AffectsEnemies") != 0;
        const bool self = skill.type == "Stim" || skill.type == "Cloak" || skill.type == "Burrow";
        const bool point = skill.type == "Storm" || skill.type == "DarkSwarm" || skill.type == "Plague";
        const string expectedCast = skill.type == "Heal"                                ? "Channel"
                                    : (skill.type == "Cloak" || skill.type == "Burrow") ? "Toggle"
                                                                                        : "Instant";
        if (skill.targetType != (self    ? "Self"
                                 : point ? "Point"
                                         : "Unit") ||
            skill.castType != expectedCast || (skill.castTimeMs && skill.type != "Burrow") ||
            (point && skill.radius <= 0) || (skill.buildings && skill.type != "Plague") ||
            (skill.type == "Stim" && skill.stackRule != "Refresh") ||
            (skill.type == "Lockdown" && skill.stackRule != "Refresh") ||
            (skill.type != "Stim" && skill.type != "Lockdown" && skill.type != "Plague" &&
             skill.stackRule != "Unique") ||
            ((skill.type == "Storm" || skill.type == "Plague" || skill.type == "Stim" ||
              skill.type == "Lockdown" || skill.type == "DarkSwarm") &&
             skill.durationMs == 0) ||
            ((skill.type == "Storm" || skill.type == "Plague") && skill.intervalMs == 0) ||
            skill.movePercent < 1 || skill.movePercent > 500 || skill.attackPercent < 1 ||
            skill.attackPercent > 500 || skill.power > 1000000 || skill.mpCost > 1000000 ||
            skill.hpCost > 1000000 || skill.mpPerSecond > 1000000 || skill.durationMs > 3600000 ||
            skill.intervalMs > 3600000 || skill.cooldownMs > 3600000 || skill.castTimeMs > 10000 ||
            skill.range > 1000000 || skill.radius > 1000000 || (skill.type == "Plague" && skill.minHP < 1))
        {
            throw runtime_error("Unsupported skill fields: " + skill.name);
        }
        data.skillDefinitions.emplace(skill.index, move(skill));
    }
    return data;
}
