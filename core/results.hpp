// Copyright (c) 2026 Max Piochowiak, Karlsruhe Institute of Technology
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <string>
#include <utility>
#include <vector>

/// One row of named values for a CSV file, e.g. the settings and measured times of a benchmark run. Several runs with
/// different settings append their rows to the same file, which can then be read with Python (pandas), a spreadsheet
/// or pgfplots for the evaluation in the thesis.
class Results
{
public:
    /// Adds a column. Numbers are written with enough digits to read them back exactly.
    void add(const std::string& name, const std::string& value) { mColumns.emplace_back(name, value); }
    void add(const std::string& name, const char* value) { mColumns.emplace_back(name, value); }
    void add(const std::string& name, bool value) { mColumns.emplace_back(name, value ? "true" : "false"); }
    void add(const std::string& name, std::integral auto value) { mColumns.emplace_back(name, std::to_string(value)); }
    void add(const std::string& name, double value);

    /// Adds the columns that describe the program run: run_name, timestamp (local time), git_commit (with "-dirty"
    /// for uncommitted changes), build_type, and the given device and timing caveats.
    void addRunEnvironment(const std::string& runName, const std::string& device, const std::string& timingCaveats);

    /// The column names, separated by commas
    std::string getHeader() const;
    /// The values, separated by commas. Values with commas, quotes or line breaks are quoted.
    std::string getRow() const;
    /// "name=value, name=value, ..." for the console
    std::string toString() const;

    /// Appends the row to the CSV file. A new or empty file gets the header first. Returns an error message if the file
    /// cannot be written or if its header differs from this row's columns, "" on success.
    std::string appendToFile(const std::string& path) const;

private:
    std::vector<std::pair<std::string, std::string>> mColumns;
};
