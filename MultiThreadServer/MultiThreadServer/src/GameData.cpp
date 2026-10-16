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
}

GameData LoadGameData(const ServerConfig& config)
{
    // 데이터는 네트워크 워커를 만들기 전에 전부 검증한다. 잘못된 상태로 접속을 받지 않는다.
    GameData data;
    data.units = LoadCsvTable(config.unitFile, "Race,UnitIndex,Name,AttackRange,HP,MP,InitialMP,Shield,Armor,AttackDamage,AttackCount,AttackIntervalMs,MoveSpeed,AttackType,DamageType,UnitSize,IsBiological,IsMechanical,SightRange,DetectionRange,Skill1,Skill2,Skill3,Skill4,Skill5");
    data.skills = LoadCsvTable(config.skillFile, "Index,SkillIndex,Name,SkillType,CastType,TargetType,TargetFilter,MpCost,HpCost,MpPerSecond,CooldownMs,CastTimeMs,DurationMs,TickIntervalMs,CastRange,AreaRadius,Power,MoveSpeedPercent,AttackSpeedPercent,MinTargetHP,AffectsBuildings,AffectsAllies,AffectsEnemies,StackRule");
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
        for (const string column : { "MpCost", "HpCost", "MpPerSecond", "CooldownMs", "CastTimeMs", "DurationMs", "TickIntervalMs", "Power", "MoveSpeedPercent", "AttackSpeedPercent", "MinTargetHP" })
        {
            table.GetInt(row, column);
        }
        table.GetNumber(row, "CastRange");
        table.GetNumber(row, "AreaRadius");
        for (const string column : { "AffectsBuildings", "AffectsAllies", "AffectsEnemies" })
        {
            ParseInt(table.Get(row, column), 0, 1);
        }
        RequireChoice(table.Get(row, "SkillType"), { "Stim", "Cloak", "Lockdown", "Heal", "Cleanse", "Blind", "Storm", "DarkSwarm", "Plague", "Burrow" });
        RequireChoice(table.Get(row, "CastType"), { "Instant", "Toggle", "Channel" });
        RequireChoice(table.Get(row, "TargetType"), { "Self", "Unit", "Point" });
        RequireChoice(table.Get(row, "TargetFilter"), { "AnyUnit", "Mechanical", "BiologicalGround", "Ground" });
        RequireChoice(table.Get(row, "StackRule"), { "Refresh", "Unique" });
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
        RequireChoice(table.Get(row, "Race"), { "Terran", "Protoss", "Zerg" });
        RequireChoice(table.Get(row, "AttackType"), { "None", "Melee", "Ranged" });
        RequireChoice(table.Get(row, "DamageType"), { "None", "Normal", "Concussive", "Explosive" });
        RequireChoice(table.Get(row, "UnitSize"), { "Small", "Medium", "Large" });
        for (const string column : { "HP", "MP", "InitialMP", "Shield", "Armor", "AttackDamage", "AttackCount", "AttackIntervalMs" })
        {
            table.GetInt(row, column);
        }
        for (const string column : { "AttackRange", "MoveSpeed", "SightRange", "DetectionRange" })
        {
            table.GetNumber(row, column);
        }
        ParseInt(table.Get(row, "IsBiological"), 0, 1);
        ParseInt(table.Get(row, "IsMechanical"), 0, 1);
        if (table.GetInt(row, "HP") == 0 || table.GetInt(row, "InitialMP") > table.GetInt(row, "MP") || table.GetNumber(row, "MoveSpeed") == 0)
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
    return data;
}
