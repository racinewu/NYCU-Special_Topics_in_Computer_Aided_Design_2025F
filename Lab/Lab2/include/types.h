#pragma once

#include "util.h"
#include <string>
#include <vector>
#include <unordered_map>

class Net;
class Cell; // forward declaration for TimingResult::Prev_Cell

// Timing analysis results - populated by STA, carried by Cell
struct TimingResult {
    double Output_Loading         = 0.0;
    double Input_Transition_Time  = 0.0;
    double Output_Transition_Time = 0.0;
    double Propagation_Delay      = 0.0;
    double Arrival_Time           = 0.0;
    bool   Worst_Case_Output      = false;
    Cell*  Prev_Cell              = nullptr; // back-pointer for path tracing
};

// Power analysis results - populated by STA::simulate()
struct PowerResult {
    double Internal_Power  = 0.0;
    double Switching_Power = 0.0;
};

// Toggle tracking - used for coverage calculation across patterns
struct ToggleInfo {
    int Last_Logic_Value = LOW;
    int Positive_Toggle  = 0;
    int Negative_Toggle  = 0;
};

// Cell - one gate instance in the netlist
class Cell {
public:
    // Circuit identity (set at parse time, never changes)
    std::string Name;
    int         Instance_Number = 0;
    Cell_Type   Type;
    std::string Lib_Cell_Name;
    bool        Controlling_Value = false; // NOR->HIGH(true), NAND->LOW(false)

    // Connectivity (set at parse time, never changes)
    std::vector<Net*> Input_Nets;
    Net*              Output_Net = nullptr;

    // Analysis results (written by STA pipeline steps)
    TimingResult Timing;
    PowerResult  Power;
    ToggleInfo   Toggle;

    Cell(const std::string& name, Cell_Type type, const std::string& lib_cell_name);
    ~Cell();
};

// Cell_LUT - timing/power table data for one cell type, from the liberty file
class Cell_LUT {
public:
    std::string Cell_Name;
    std::unordered_map<std::string, double>              Pin_Cap;       // pin -> cap (pF)
    std::unordered_map<std::string, std::vector<double>> Look_Up_Tables;// key -> flat values
    std::unordered_map<std::string, std::string>         Table_Template; // key -> template name

    explicit Cell_LUT(const std::string& cell_name);
    ~Cell_LUT() = default;

    double                     Get_Pin_Cap      (const std::string& pin_name)   const;
    const std::vector<double>& Get_Look_Up_Table(const std::string& table_name) const;
};

// Library - all cell LUTs + index arrays per template, from one .lib file
class Library {
public:
    // template name -> { index_1 (output loading), index_2 (input slew) }
    std::unordered_map<std::string,
        std::pair<std::vector<double>, std::vector<double>>> Templates;

    std::unordered_map<std::string, Cell_LUT*> Cell_LUT_Map;

    Library()  = default;
    ~Library();

    const std::vector<double>& Get_Cell_Table(const std::string& cell_name,
                                               const std::string& table_name) const;
};

// Net - one signal wire in the netlist
class Net {
public:
    std::string Name;
    Net_Type    Type;
    int         Logic_Value = UNSET;

    std::pair<std::string, Cell*>              Input_Cells;  // (pin, driving cell)
    std::vector<std::pair<std::string, Cell*>> Output_Cells; // (pin, fanout cells)

    Net(const std::string& name, Net_Type type);
    ~Net() = default;
};