#pragma once
#include "GameData.h"
#include "Packet.h"

struct stMovementInput
{
    double x;
    double y;
    double goalX;
    double goalY;
    double step;
    bool moving;
    int mp;
    int maxMP;
    int regen;
};
struct stMovementResult
{
    double x;
    double y;
    bool moving;
    int mp;
    bool operator==(const stMovementResult&) const = default;
};
struct stDamageResult
{
    int hp;
    int shield;
    bool operator==(const stDamageResult&) const = default;
};

// 입력과 출력이 분리되어 같은 함수를 직렬/병렬로 실행하고 결과를 비교할 수 있다.
stMovementResult CalculateMovement(const stMovementInput& input,
                                   const stMapConfig& map,
                                   const array<wire::BuildingState, 3>& buildings);
stDamageResult CalculateStormDamage(int hp, int shield, int damage);
bool CanStand(double x, double y, const stMapConfig& map, const array<wire::BuildingState, 3>& buildings);
int RunBattleChecks();
