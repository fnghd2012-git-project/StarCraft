#include "CsvTable.h"

namespace
{
vector<string> SplitCsv(const string& line)
{
    // 쉼표로 나누되 큰따옴표 안의 쉼표와 "" 이스케이프는 한 필드에 남긴다.
    // 게임 설정 CSV는 한 행이 한 레코드이며, 필드 안 줄바꿈은 허용하지 않는다.
    vector<string> fields;
    size_t position = 0;
    while (true)
    {
        string field;
        if (position < line.size() && line[position] == '"')
        {
            ++position;
            bool closed = false;
            while (position < line.size())
            {
                const char ch = line[position++];
                if (ch != '"')
                {
                    field += ch;
                }
                else if (position < line.size() && line[position] == '"')
                {
                    field += '"';
                    ++position;
                }
                else
                {
                    closed = true;
                    break;
                }
            }
            if (!closed || (position < line.size() && line[position] != ','))
            {
                throw runtime_error("Invalid CSV quote");
            }
        }
        else
        {
            while (position < line.size() && line[position] != ',')
            {
                if (line[position] == '"')
                {
                    throw runtime_error("Unexpected CSV quote");
                }
                field += line[position++];
            }
        }
        fields.push_back(move(field));
        if (position == line.size())
        {
            break;
        }
        ++position;
    }
    return fields;
}

} // namespace

stCsvTable LoadCsvTable(const fs::path& path, const string& expectedHeader)
{
    // 열 이름과 순서를 고정해 엑셀에서 열을 잘못 옮긴 경우 시작 시 발견한다.
    ifstream input(path);
    if (!input)
    {
        throw runtime_error("Cannot open CSV: " + path.string());
    }
    stCsvTable table;
    string line;
    int lineNumber = 0;
    while (getline(input, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            line.erase(0, 3);
        }
        try
        {
            auto fields = SplitCsv(line);
            if (lineNumber == 1)
            {
                table.columns = move(fields);
                if (table.columns != SplitCsv(expectedHeader))
                {
                    throw runtime_error("CSV header does not match schema");
                }
                continue;
            }
            if (fields.size() != table.columns.size())
            {
                throw runtime_error("CSV column count mismatch");
            }
            for (const auto& field : fields)
            {
                if (field.empty())
                {
                    throw runtime_error("CSV contains an empty value");
                }
            }
            table.rows.push_back(move(fields));
        }
        catch (const exception& error)
        {
            throw runtime_error(path.filename().string() + ":" + to_string(lineNumber) + ": " + error.what());
        }
    }
    if (input.bad() || table.rows.empty())
    {
        throw runtime_error("Empty or unreadable CSV: " + path.string());
    }
    return table;
}

const string& stCsvTable::Get(size_t row, string_view column) const
{
    // 열 개수가 작고 시작할 때만 사용하므로 복잡한 인덱스 캐시 없이 순서대로 찾는다.
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (columns[i] == column)
        {
            return rows.at(row).at(i);
        }
    }
    throw runtime_error("Unknown column: " + string(column));
}

int stCsvTable::GetInt(size_t row, string_view column) const
{
    return ParseInt(Get(row, column), 0, 1000000);
}

double stCsvTable::GetNumber(size_t row, string_view column) const
{
    const double value = ParseNumber(Get(row, column));
    if (value < 0 || value > 1000000)
    {
        throw runtime_error("Number outside data range: " + string(column));
    }
    return value;
}
