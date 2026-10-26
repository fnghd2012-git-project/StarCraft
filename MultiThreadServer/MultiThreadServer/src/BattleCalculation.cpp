#include "BattleCalculation.h"
#include "BattleWorkers.h"

bool CanStand(double x, double y, const stMapConfig& map, const array<wire::BuildingState, 3>& buildings)
{
    if (!isfinite(x) || !isfinite(y) || x < map.minX || x > map.maxX || y < map.minY || y > map.maxY)
    {
        return false;
    }
    for (const auto& building : buildings)
    {
        const double dx = x - building.x(), dy = y - building.y();
        if (building.hp() > 0 && dx * dx + dy * dy <= building.radius() * building.radius())
        {
            return false;
        }
    }
    return true;
}

stMovementResult CalculateMovement(const stMovementInput& input,
                                   const stMapConfig& map,
                                   const array<wire::BuildingState, 3>& buildings)
{
    stMovementResult result{input.x, input.y, input.moving, min(input.maxMP, input.mp + input.regen)};
    if (!input.moving)
    {
        return result;
    }
    const double dx = input.goalX - input.x, dy = input.goalY - input.y;
    const double distance = sqrt(dx * dx + dy * dy), step = min(distance, input.step);
    if (distance < 0.000001)
    {
        result.moving = false;
        return result;
    }
    const double x = input.x + dx / distance * step, y = input.y + dy / distance * step;
    bool blocked = !CanStand(x, y, map, buildings);
    // 끝점뿐 아니라 선분과 건물의 충돌을 검사해 빠른 이동의 관통을 막는다.
    const double sx = x - input.x, sy = y - input.y;
    for (const auto& building : buildings)
    {
        if (building.hp() == 0 || sx * sx + sy * sy == 0)
        {
            continue;
        }
        const double ratio = clamp(
            ((building.x() - input.x) * sx + (building.y() - input.y) * sy) / (sx * sx + sy * sy), 0.0, 1.0);
        const double bx = input.x + sx * ratio - building.x(), by = input.y + sy * ratio - building.y();
        if (bx * bx + by * by <= building.radius() * building.radius())
        {
            blocked = true;
        }
    }
    if (blocked)
    {
        result.moving = false;
    }
    else
    {
        result.x = x;
        result.y = y;
        result.moving = step < distance;
    }
    return result;
}

stDamageResult CalculateStormDamage(int hp, int shield, int damage)
{
    const int absorbed = min(shield, damage);
    return {max(0, hp - (damage - absorbed)), shield - absorbed};
}

int RunBattleChecks()
{
    try
    {
        stMapConfig map;
        map.minX = map.minY = 0;
        map.maxX = map.maxY = 200;
        array<wire::BuildingState, 3> buildings;
        buildings[0].set_hp(100);
        buildings[0].set_x(100);
        buildings[0].set_y(100);
        buildings[0].set_radius(5);
        vector<stMovementInput> input;
        for (int i = 0; i < 1500; ++i)
        {
            input.push_back({double(i % 200),
                             double(i / 200),
                             double((i * 7) % 220),
                             double((i * 11) % 220),
                             3,
                             true,
                             i % 100,
                             100,
                             1});
        }
        input[0] = {90, 100, 110, 100, 20, true, 100, 100, 1}; // 건물 관통
        input[1] = {0, 0, -10, 0, 3, true, 0, 100, 0};         // 맵 경계
        vector<stMovementResult> expected(input.size()), result(input.size());
        vector<stDamageResult> damage(input.size()), reference(input.size());
        for (const int count : {1, 2, 4})
        {
            CBattleWorkers workers(count);
            for (int tick = 0; tick < 200; ++tick)
            {
                workers.Run(input.size(),
                            [&](size_t i)
                            {
                                result[i] = CalculateMovement(input[i], map, buildings);
                                damage[i] = CalculateStormDamage(int(i % 100), int(i % 20), tick % 40);
                            });
                for (size_t i = 0; i < input.size(); ++i)
                {
                    expected[i] = CalculateMovement(input[i], map, buildings);
                    reference[i] = CalculateStormDamage(int(i % 100), int(i % 20), tick % 40);
                }
                if (result != expected || damage != reference)
                {
                    throw runtime_error("Parallel result mismatch");
                }
            }
            if (result[0].x != 90 || result[1].x != 0 || result[0].mp != 100)
            {
                throw runtime_error("Movement boundary check failed");
            }
            // 예외가 있어도 모든 워커가 완료되고 다음 작업을 받을 수 있어야 한다.
            bool caught = false;
            try
            {
                workers.Run(16,
                            [](size_t i)
                            {
                                if (i == 3)
                                {
                                    throw runtime_error("test");
                                }
                            });
            }
            catch (const exception&)
            {
                caught = true;
            }
            if (!caught)
            {
                throw runtime_error("Worker exception lost");
            }
            workers.Run(16,
                        [](size_t)
                        {
                        });
            cout << "[OK] Parallel calculations: workers=" << count
                 << " units=1500 rounds=200 position/mp/hp/shield exact; exception recovery\n";
        }
        if (CalculateStormDamage(5, 10, 20) != stDamageResult{0, 0})
        {
            throw runtime_error("Lethal damage check failed");
        }
        return 0;
    }
    catch (const exception& error)
    {
        cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
