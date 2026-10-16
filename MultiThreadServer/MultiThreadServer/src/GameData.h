#pragma once

#include "CsvTable.h"

// 시작할 때만 읽는다. 실행 중인 워커에는 수정 권한을 주지 않는다.
struct GameData
{
    CsvTable units;
    CsvTable skills;
};

GameData LoadGameData(const ServerConfig& config);
