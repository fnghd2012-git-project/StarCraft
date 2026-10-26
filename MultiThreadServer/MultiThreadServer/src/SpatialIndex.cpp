#include "SpatialIndex.h"

unsigned long long CSpatialIndex::CellKey(unsigned x, unsigned y)
{
    // 좌표 상한은 1,000,000이다. 두 셀 좌표를 겹치지 않는 32비트 구간에 넣는다.
    return (static_cast<unsigned long long>(x) << 32) | y;
}

void CSpatialIndex::Rebuild(vector<stSpatialPoint> points, double cellSize, bool buildGrid)
{
    if (!isfinite(cellSize) || cellSize < 1 || cellSize > 1000000)
    {
        throw runtime_error("Invalid spatial cell size");
    }
    for (const auto& point : points)
    {
        if (!isfinite(point.x) || !isfinite(point.y) || point.x < 0 || point.y < 0 || point.x > 1000000 ||
            point.y > 1000000)
        {
            throw runtime_error("Invalid spatial point");
        }
    }
    m_points = move(points);
    m_cellSize = cellSize;
    m_cells.clear();
    m_gridBuilt = buildGrid;
    if (!buildGrid)
    {
        return;
    }
    // 맵 크기만큼 배열을 만들지 않는다. 실제 유닛이 있는 셀만 저장한다.
    for (size_t i = 0; i < m_points.size(); ++i)
    {
        const auto& point = m_points[i];
        m_cells[CellKey(static_cast<unsigned>(point.x / m_cellSize),
                        static_cast<unsigned>(point.y / m_cellSize))]
            .push_back(i);
    }
}

void CSpatialIndex::CheckQuery(double x, double y, double radius)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(radius) || x < 0 || y < 0 || x > 1000000 || y > 1000000 ||
        radius < 0 || radius > 1000000)
    {
        throw runtime_error("Invalid spatial query");
    }
}

bool CSpatialIndex::InRange(size_t index, double x, double y, double squaredRadius) const
{
    const auto& point = m_points[index];
    const double dx = point.x - x, dy = point.y - y;
    return dx * dx + dy * dy <= squaredRadius;
}

size_t CSpatialIndex::QueryLinear(double x, double y, double radius, vector<size_t>& result) const
{
    CheckQuery(x, y, radius);
    result.clear();
    const double squaredRadius = radius * radius;
    for (size_t i = 0; i < m_points.size(); ++i)
    {
        if (InRange(i, x, y, squaredRadius))
        {
            result.push_back(i);
        }
    }
    return m_points.size();
}

size_t CSpatialIndex::QueryGrid(double x, double y, double radius, vector<size_t>& result) const
{
    CheckQuery(x, y, radius);
    if (!m_gridBuilt)
    {
        throw runtime_error("Spatial grid was not built");
    }
    result.clear();
    if (m_points.empty())
    {
        return 0;
    }
    const unsigned minX = static_cast<unsigned>(max(0.0, x - radius) / m_cellSize);
    const unsigned minY = static_cast<unsigned>(max(0.0, y - radius) / m_cellSize);
    const unsigned maxX = static_cast<unsigned>((x + radius) / m_cellSize);
    const unsigned maxY = static_cast<unsigned>((y + radius) / m_cellSize);
    const auto cellCount = static_cast<unsigned long long>(maxX - minX + 1) * (maxY - minY + 1);
    // 작은 셀 + 큰 반경 설정에서 빈 셀을 수억 번 순회하지 않도록 상한을 둔다.
    // 이 경우에도 같은 거리 판정으로 전체 검색하므로 결과는 변하지 않는다.
    if (cellCount > m_points.size())
    {
        return QueryLinear(x, y, radius, result);
    }
    size_t checked = 0;
    const double squaredRadius = radius * radius;
    for (unsigned cy = minY; cy <= maxY; ++cy)
    {
        for (unsigned cx = minX; cx <= maxX; ++cx)
        {
            const auto cell = m_cells.find(CellKey(cx, cy));
            if (cell == m_cells.end())
            {
                continue;
            }
            checked += cell->second.size();
            for (const size_t index : cell->second)
            {
                if (InRange(index, x, y, squaredRadius))
                {
                    result.push_back(index);
                }
            }
        }
    }
    return checked;
}
