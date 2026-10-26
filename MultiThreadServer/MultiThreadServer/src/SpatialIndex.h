#pragma once
#include "Common.h"

enum class SpatialSearchMode
{
    Linear,
    Grid
};
inline const char* SpatialSearchName(SpatialSearchMode mode)
{
    return mode == SpatialSearchMode::Grid ? "Grid" : "Linear";
}

struct stSpatialPoint
{
    double x = 0;
    double y = 0;
};

// 좌표 사본은 메인에서 만든다. 다음 Rebuild 전까지 여러 워커가 읽기 전용으로 검색한다.
// 결과는 입력 points의 인덱스이며, 종족/은신/정렬/인원 제한은 공통 게임 로직이 처리한다.
class CSpatialIndex final
{
public:
    void Rebuild(vector<stSpatialPoint> points, double cellSize, bool buildGrid);
    size_t QueryLinear(double x, double y, double radius, vector<size_t>& result) const;
    size_t QueryGrid(double x, double y, double radius, vector<size_t>& result) const;

private:
    static unsigned long long CellKey(unsigned x, unsigned y);
    static void CheckQuery(double x, double y, double radius);
    bool InRange(size_t index, double x, double y, double squaredRadius) const;
    vector<stSpatialPoint> m_points;
    unordered_map<unsigned long long, vector<size_t>> m_cells;
    double m_cellSize = 25;
    bool m_gridBuilt = false;
};
