#pragma once

#include "Config.h"

// CSV 원본 값을 보관한다. 숫자 변환과 범위 검사는 로딩 시 한 번 수행한다.
struct stCsvTable
{
    vector<string> columns;
    vector<vector<string>> rows;

    const string& Get(size_t row, string_view column) const;
    int GetInt(size_t row, string_view column) const;
    double GetNumber(size_t row, string_view column) const;
};

stCsvTable LoadCsvTable(const fs::path& path, const string& expectedHeader);
