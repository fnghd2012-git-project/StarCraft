#pragma once
#include "Common.h"

// DB/소켓 없이 동일 좌표를 두 검색 방식에 넣어 결과와 검색 비용을 비교한다.
int RunSpatialChecks(const fs::path& csvPath);
