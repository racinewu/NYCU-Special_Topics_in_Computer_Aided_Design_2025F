#pragma once

#include "types.h"
#include "parser.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

// Modes for Set_Cell_Input_Transition_Time
enum class TimingMode { WorstCase, PatternAware };

class STA {
public:
    STA()  = default;
    ~STA();

    // Single entry point: load files, run full analysis, write all output files.
    void analyze(const std::string& netlistFile,
             const std::string& libraryFile,
             const std::string& patternFile);

private:
    std::string Lib_Name;
    std::string Mod_Name;

    Library*                               library = nullptr;
    std::unordered_map<std::string, Cell*> Cells;
    std::unordered_map<std::string, Net*>  Nets;
    size_t Num_Cells = 0;
    size_t Num_Nets  = 0;

    std::vector<Cell*>                                 Primary_Output_Cells;
    std::vector<Cell*>                                 Cells_In_Topological_Order;
    std::vector<std::string>                           Input_Order;
    std::vector<std::unordered_map<std::string, int>>  Patterns;

    double            maxDelay = -1e300;
    double            minDelay =  1e300;
    std::vector<Net*> Longest_Path;
    std::vector<Net*> Shortest_Path;

    // Pipeline steps
    void Cell_Topological_Sort();
    void Calculate_Output_Loading();
    void Calculate_Cell_Delay();
    void pathFinding();
    void assign_pattern();

    // Timing
    void   Set_Cell_Input_Transition_Time(Cell* cell, TimingMode mode);
    double Table_Interpolation(double x_req, double y_req,
                               const std::vector<double>& x_index,
                               const std::vector<double>& y_index,
                               const std::vector<double>& table);
    double Table_Look_Up(Cell* cell, const std::string& table_name);

    // Output helpers
    std::vector<Cell*> getSortedCells(SortKey key) const;
    bool compareCells(const Cell* c1, const Cell* c2, SortKey key, bool ascending) const;

    void Dump_Output_Loading(SortKey key);
    void Dump_Cell_Delay    (SortKey key);
    void Dump_Longest_And_Shortest_Delay_And_Path();

    void dump_gate_info (std::ofstream& fout, SortKey key);
    void dump_gate_power(std::ofstream& fout, SortKey key);
    void dump_coverage  (std::ofstream& fout, double total_power,
                         int pattern_index, SortKey key);

    // Path tracing / simulation
    std::vector<Net*> findPath(Cell* cell);
    double simulate(int pattern_index);
};