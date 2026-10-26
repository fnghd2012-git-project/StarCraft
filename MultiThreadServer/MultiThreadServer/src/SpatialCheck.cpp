#include "SpatialCheck.h"
#include "SpatialIndex.h"

namespace
{
void Require(bool condition, const string& message)
{
    if (!condition)
    {
        throw runtime_error("Spatial check: " + message);
    }
}

void CheckQuery(
    const CSpatialIndex& index, const vector<stSpatialPoint>& points, double x, double y, double radius)
{
    vector<size_t> expected, linear, grid;
    // 검색 구현과 독립적으로 전체 좌표를 검사하는 기준 결과다.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const double dx = points[i].x - x, dy = points[i].y - y;
        if (dx * dx + dy * dy <= radius * radius)
        {
            expected.push_back(i);
        }
    }
    const auto linearChecks = index.QueryLinear(x, y, radius, linear);
    const auto gridChecks = index.QueryGrid(x, y, radius, grid);
    sort(grid.begin(), grid.end());
    Require(linear == expected && grid == expected, "candidate membership differs");
    Require(linearChecks == points.size() && gridChecks <= linearChecks, "invalid candidate counter");
}

vector<stSpatialPoint> MakePoints(bool clustered)
{
    mt19937 random(20261018);
    uniform_int_distribution<int> coordinate(0, clustered ? 1000 : 12000);
    vector<stSpatialPoint> points;
    for (int i = 0; i < 1500; ++i)
    {
        const double offset = clustered ? 55.0 : 0.0;
        points.push_back({offset + coordinate(random) / 100.0, offset + coordinate(random) / 100.0});
    }
    return points;
}

struct stMeasurement
{
    double buildUs = 0, queryUs = 0;
    unsigned long long candidates = 0, matches = 0, checksum = 0;
};

stMeasurement Measure(const vector<stSpatialPoint>& points, bool grid)
{
    stMeasurement result;
    CSpatialIndex index;
    vector<size_t> found;
    const auto begin = chrono::steady_clock::now();
    index.Rebuild(points, 25, grid);
    const auto queries = chrono::steady_clock::now();
    for (const auto& point : points)
    {
        result.candidates += grid ? index.QueryGrid(point.x, point.y, 25, found)
                                  : index.QueryLinear(point.x, point.y, 25, found);
        result.matches += found.size();
        // 결과 소비도 양쪽에서 동일하게 수행한다. 순서와 무관한 합계이며 무결성 서명은 아니다.
        for (const auto i : found)
        {
            result.checksum += i + 1;
        }
    }
    const auto end = chrono::steady_clock::now();
    result.buildUs = chrono::duration<double, micro>(queries - begin).count();
    result.queryUs = chrono::duration<double, micro>(end - queries).count();
    return result;
}
} // namespace

int RunSpatialChecks(const fs::path& csvPath)
{
    try
    {
        CSpatialIndex index;
        vector<stSpatialPoint> edges{
            {0, 0}, {25, 0}, {24.999999, 0}, {25.000001, 0}, {25, 25}, {25, 25}, {1000000, 1000000}};
        for (const double cellSize : {1.0, 7.5, 25.0, 100.0, 1000000.0})
        {
            index.Rebuild(edges, cellSize, true);
            for (const auto& center : edges)
            {
                for (const double radius : {0.0, 25.0, 1000000.0})
                {
                    CheckQuery(index, edges, center.x, center.y, radius);
                }
            }
        }
        // 새 좌표/삭제/빈 전장으로 교체해 이전 셀과 인덱스가 남지 않는지 확인한다.
        edges = {{90, 90}, {0, 0}};
        index.Rebuild(edges, 25, true);
        CheckQuery(index, edges, 0, 0, 25);
        index.Rebuild({}, 25, true);
        CheckQuery(index, {}, 25, 25, 25);
        bool rejected = false;
        try
        {
            index.Rebuild({{-1, 0}}, 25, true);
        }
        catch (const exception&)
        {
            rejected = true;
        }
        Require(rejected, "negative point accepted");
        rejected = false;
        try
        {
            index.Rebuild({}, 0, true);
        }
        catch (const exception&)
        {
            rejected = true;
        }
        Require(rejected, "zero cell size accepted");
        rejected = false;
        try
        {
            vector<size_t> found;
            index.QueryLinear(numeric_limits<double>::quiet_NaN(), 0, 25, found);
        }
        catch (const exception&)
        {
            rejected = true;
        }
        Require(rejected, "NaN query accepted");
        for (const bool clustered : {false, true})
        {
            const auto points = MakePoints(clustered);
            for (const double cellSize : {7.5, 25.0, 60.0})
            {
                index.Rebuild(points, cellSize, true);
                for (const auto& point : points)
                {
                    CheckQuery(index, points, point.x, point.y, 25);
                }
            }
        }
        cout << "[OK] Spatial equality: edges, radius boundary, duplicates, rebuild/removal, empty, invalid "
                "input, 9000 population queries"
             << endl;
        if (csvPath.empty())
        {
            return 0;
        }
        if (!csvPath.parent_path().empty())
        {
            fs::create_directories(csvPath.parent_path());
        }
        ofstream csv(csvPath);
        if (!csv)
        {
            throw runtime_error("Cannot open spatial benchmark CSV");
        }
        csv << "Scenario,Players,Pass,Mode,CellSize,Radius,BuildUs,QueryUs,TotalUs,CandidateChecks,Matches,"
               "Checksum\n"
            << fixed << setprecision(3);
        for (const bool clustered : {false, true})
        {
            const auto points = MakePoints(clustered);
            // 두 번 예열한 뒤 20쌍을 기록한다. 매 회 실행 순서를 바꿔 순서 편향을 줄인다.
            for (int pass = -2; pass < 20; ++pass)
            {
                array<stMeasurement, 2> results;
                for (int order = 0; order < 2; ++order)
                {
                    const int mode = (pass + 2 + order) % 2;
                    results[mode] = Measure(points, mode == 1);
                }
                Require(results[0].matches == results[1].matches &&
                            results[0].checksum == results[1].checksum,
                        "benchmark result differs");
                if (pass < 0)
                {
                    continue;
                }
                for (int mode = 0; mode < 2; ++mode)
                {
                    const auto& result = results[mode];
                    csv << (clustered ? "Clustered" : "Uniform") << ",1500," << pass + 1 << ','
                        << (mode ? "Grid" : "Linear") << ",25,25," << result.buildUs << ',' << result.queryUs
                        << ',' << result.buildUs + result.queryUs << ',' << result.candidates << ','
                        << result.matches << ',' << result.checksum << '\n';
                }
            }
        }
        csv.close();
        if (!csv)
        {
            throw runtime_error("Spatial CSV write failed");
        }
        cout << "[OK] Spatial benchmark CSV: " << csvPath.string() << " (search only; not server RTT)"
             << endl;
        return 0;
    }
    catch (const exception& error)
    {
        cerr << "[FAIL] " << error.what() << endl;
        return 1;
    }
}
