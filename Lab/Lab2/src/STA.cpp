#include "STA.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <queue>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <array>
#include <unordered_set>

using namespace std;

// run
// Opens input files, delegates parsing to Parser, then runs the four analysis steps in order.
void STA::analyze(const string& netlistFile,
              const string& libraryFile,
              const string& patternFile)
{
    ifstream netlistStream(netlistFile);
    ifstream libraryStream(libraryFile);
    ifstream patternStream(patternFile);

    if (!libraryStream) throw runtime_error("Cannot open library file: "  + libraryFile);
    if (!netlistStream) throw runtime_error("Cannot open netlist file: "  + netlistFile);
    if (!patternStream) throw runtime_error("Cannot open pattern file: "  + patternFile);

    unordered_map<string, unordered_map<string, string>> pin_dirs;
    library = Parser::parseLibrary(libraryStream, Lib_Name, pin_dirs);
    if (!library) throw runtime_error("Library parsing failed.");

    ParsedNetlist netlist = Parser::parseNetlist(netlistStream, pin_dirs, library);
    Mod_Name             = move(netlist.Mod_Name);
    Cells                = move(netlist.Cells);
    Nets                 = move(netlist.Nets);
    Primary_Output_Cells = move(netlist.Primary_Output_Cells);
    Num_Cells = Cells.size();
    Num_Nets  = Nets.size();

    ParsedPatterns pats = Parser::parsePatterns(patternStream);
    Input_Order = move(pats.Input_Order);
    Patterns    = move(pats.Patterns);

    Cell_Topological_Sort();
    Calculate_Output_Loading();
    Calculate_Cell_Delay();
    pathFinding();
    assign_pattern();
}

STA::~STA() {
    delete library;
    for (auto& [k, v] : Cells) delete v;
    for (auto& [k, v] : Nets)  delete v;
}

// Cell_Topological_Sort
// Kahn's BFS on in-degree. Populates Cells_In_Topological_Order; throws on cycle.
void STA::Cell_Topological_Sort() {
    unordered_map<Cell*, int> indegree;
    indegree.reserve(Num_Cells);

    for (auto& [name, cell] : Cells) {
        int deg = 0;
        for (auto* net : cell->Input_Nets)
            if (net->Type != Net_Type::Input) ++deg;
        indegree[cell] = deg;
    }

    queue<Cell*> q;
    for (auto& [cell, deg] : indegree)
        if (deg == 0) q.push(cell);

    Cells_In_Topological_Order.clear();
    Cells_In_Topological_Order.reserve(Num_Cells);

    while (!q.empty()) {
        Cell* curr = q.front(); q.pop();
        Cells_In_Topological_Order.push_back(curr);
        if (!curr->Output_Net) continue;
        for (auto& [pin, next] : curr->Output_Net->Output_Cells)
            if (--indegree[next] == 0) q.push(next);
    }

    if (Cells_In_Topological_Order.size() != Num_Cells)
        throw runtime_error("Cycle detected in netlist.");
}

// Calculate_Output_Loading
// Sums fanout pin capacitances; primary outputs get PRIMARY_OUTPUT_LOADING (0.03 pF).
void STA::Calculate_Output_Loading() {
    for (Cell* cell : Cells_In_Topological_Order) {
        double loading = 0.0;
        if (cell->Output_Net) {
            if (cell->Output_Net->Type == Net_Type::Output)
                loading += PRIMARY_OUTPUT_LOADING;
            for (auto& [pin_name, fanout] : cell->Output_Net->Output_Cells) {
                auto it = library->Cell_LUT_Map.find(fanout->Lib_Cell_Name);
                if (it == library->Cell_LUT_Map.end()) {
                    cerr << "Error: lib cell not found: " << fanout->Lib_Cell_Name << "\n";
                    abort();
                }
                auto cap_it = it->second->Pin_Cap.find(pin_name);
                if (cap_it == it->second->Pin_Cap.end()) {
                    cerr << "Error: pin cap not found: " << pin_name << "\n";
                    abort();
                }
                loading += cap_it->second;
            }
        }
        cell->Timing.Output_Loading = loading;
    }
    Dump_Output_Loading(SortKey::InstanceNumber);
}

// Table_Interpolation
// Bilinear interpolation over a flat row-major 2-D LUT. Clamps at boundaries.
double STA::Table_Interpolation(double x_req, double y_req,
                                 const vector<double>& x_index,
                                 const vector<double>& y_index,
                                 const vector<double>& table) {
    size_t n_col = x_index.size();
    size_t n_row = y_index.size();
    assert(table.size() == n_col * n_row);

    size_t xi = lower_bound(x_index.begin(), x_index.end(), x_req) - x_index.begin();
    size_t yi = lower_bound(y_index.begin(), y_index.end(), y_req) - y_index.begin();

    if (xi == 0)     xi = 1;
    if (yi == 0)     yi = 1;
    if (xi >= n_col) xi = n_col - 1;
    if (yi >= n_row) yi = n_row - 1;

    double x1 = x_index[xi-1], x2 = x_index[xi];
    double y1 = y_index[yi-1], y2 = y_index[yi];

    double P00 = table[(xi-1) + n_col*(yi-1)];
    double P01 = table[(xi-1) + n_col* yi   ];
    double P10 = table[ xi    + n_col*(yi-1)];
    double P11 = table[ xi    + n_col* yi   ];

    double tx = (x2 == x1) ? 0.0 : (x_req - x1) / (x2 - x1);
    double ty = (y2 == y1) ? 0.0 : (y_req - y1) / (y2 - y1);

    double A = P00 + (P01 - P00) * ty;
    double B = P10 + (P11 - P10) * ty;
    return A + (B - A) * tx;
}

// Table_Look_Up
// Looks up cell's named LUT table and interpolates with current Output_Loading and Input_Transition_Time.
// Falls back to the first template if the registered template name is not found.
double STA::Table_Look_Up(Cell* cell, const string& table_name) {
    auto cit = library->Cell_LUT_Map.find(cell->Lib_Cell_Name);
    if (cit == library->Cell_LUT_Map.end()) {
        cerr << "Error: cell not in library: " << cell->Lib_Cell_Name << "\n"; abort();
    }
    Cell_LUT* lut = cit->second;
    const vector<double>& tbl = lut->Get_Look_Up_Table(table_name);

    const vector<double>* x_idx = nullptr;
    const vector<double>* y_idx = nullptr;

    auto tmplIt = lut->Table_Template.find(table_name);
    if (tmplIt != lut->Table_Template.end()) {
        auto idxIt = library->Templates.find(tmplIt->second);
        if (idxIt != library->Templates.end()) {
            x_idx = &idxIt->second.first;
            y_idx = &idxIt->second.second;
        }
    }
    if (!x_idx && !library->Templates.empty()) {
        x_idx = &library->Templates.begin()->second.first;
        y_idx = &library->Templates.begin()->second.second;
    }
    if (!x_idx || x_idx->empty() || !y_idx || y_idx->empty()) {
        cerr << "Error: no valid index for table " << table_name << "\n"; abort();
    }

    return Table_Interpolation(cell->Timing.Output_Loading,
                               cell->Timing.Input_Transition_Time,
                               *x_idx, *y_idx, tbl);
}

// ==========================================================
// Set_Cell_Input_Transition_Time
// Sets Input_Transition_Time and Arrival_Time by tracing back to the driving cell(s).
// WorstCase:    picks the latest-arriving input regardless of logic value (Step 2).
// PatternAware: picks the input carrying the controlling value when present (Step 4).
//   NOR controlling value = HIGH, NAND = LOW, INV is single-input so unused.
// ==========================================================
void STA::Set_Cell_Input_Transition_Time(Cell* cell, TimingMode mode) {
    auto arrivalOf = [](const Cell* pre) {
        return pre->Timing.Arrival_Time + pre->Timing.Propagation_Delay + WIRE_DELAY;
    };
    // Commit the chosen predecessor; a null pre means a PI dominates (arrival = 0).
    auto apply = [&](Cell* pre, double arrival) {
        cell->Timing.Prev_Cell             = pre;
        cell->Timing.Arrival_Time          = pre ? arrival : 0.0;
        cell->Timing.Input_Transition_Time = pre ? pre->Timing.Output_Transition_Time : 0.0;
    };

    // Collect every non-PI input as a candidate driver.
    struct Drv { Cell* pre; double arr; bool ctrl; };
    array<Drv, 2> drv;
    int nd = 0;
    bool pi_has_ctrl = false;
    for (Net* n : cell->Input_Nets) {
        bool ctrl = (n->Logic_Value == (int)cell->Controlling_Value);
        if (n->Type == Net_Type::Input) { pi_has_ctrl |= ctrl; continue; }
        Cell* pre = n->Input_Cells.second;
        assert(pre);
        drv[nd++] = { pre, arrivalOf(pre), ctrl };
    }

    if (nd == 0) { apply(nullptr, 0.0); return; } // all inputs are PIs

    if (mode == TimingMode::PatternAware) {
        // A PI carrying the controlling value alone determines the output.
        if (pi_has_ctrl) { apply(nullptr, 0.0); return; }
        // Among drivers carrying the controlling value, take the earliest arrival.
        const Drv* best = nullptr;
        for (int i = 0; i < nd; ++i)
            if (drv[i].ctrl && (!best || drv[i].arr < best->arr)) best = &drv[i];
        if (best) { apply(best->pre, best->arr); return; }
    }

    // WorstCase, or PatternAware with no controlling value: take the latest arrival.
    const Drv* best = &drv[0];
    for (int i = 1; i < nd; ++i)
        if (drv[i].arr > best->arr) best = &drv[i];
    apply(best->pre, best->arr);
}

// Calculate_Cell_Delay (Step 2)
// Picks worst-case output (larger of rise/fall delay) for each cell; writes delay.txt.
void STA::Calculate_Cell_Delay() {
    for (Cell* cell : Cells_In_Topological_Order) {
        if (!library->Cell_LUT_Map.count(cell->Lib_Cell_Name)) {
            cerr << "Unsupported cell: " << cell->Lib_Cell_Name << "\n"; abort();
        }
        Set_Cell_Input_Transition_Time(cell, TimingMode::WorstCase);

        double rise     = Table_Look_Up(cell, "cell_rise");
        double fall     = Table_Look_Up(cell, "cell_fall");
        double out_rise = Table_Look_Up(cell, "rise_transition");
        double out_fall = Table_Look_Up(cell, "fall_transition");

        if (fall > rise) {
            cell->Timing.Propagation_Delay     = fall;
            cell->Timing.Output_Transition_Time = out_fall;
            cell->Timing.Worst_Case_Output      = false;
        } else {
            cell->Timing.Propagation_Delay     = rise;
            cell->Timing.Output_Transition_Time = out_rise;
            cell->Timing.Worst_Case_Output      = true;
        }
    }
    Dump_Cell_Delay(SortKey::InstanceNumber);
}

// pathFinding (Step 3)
// Finds max/min total delay across primary outputs and traces Prev_Cell back-pointers into net sequences.

// findPath
// Walks Prev_Cell back-pointers to the PI net, collecting output nets; reverses for source-to-sink order.
vector<Net*> STA::findPath(Cell* cell) {
    vector<Net*> path;
    while (cell) {
        path.push_back(cell->Output_Net);
        if (!cell->Timing.Prev_Cell && !cell->Input_Nets.empty())
            path.push_back(cell->Input_Nets[0]);
        cell = cell->Timing.Prev_Cell;
    }
    reverse(path.begin(), path.end());
    return path;
}

void STA::pathFinding() {
    Cell *longestCell = nullptr, *shortestCell = nullptr;
    maxDelay = -1e300;
    minDelay =  1e300;

    for (Cell* cell : Primary_Output_Cells) {
        double d = cell->Timing.Arrival_Time + cell->Timing.Propagation_Delay;
        if (d > maxDelay) { maxDelay = d; longestCell  = cell; }
        if (d < minDelay) { minDelay = d; shortestCell = cell; }
    }
    assert(longestCell && shortestCell);

    Longest_Path  = findPath(longestCell);
    Shortest_Path = findPath(shortestCell);
    Dump_Longest_And_Shortest_Delay_And_Path();
}

// simulate (Step 4 helper)
// Propagates one input pattern, resolves delays via PatternAware mode, accumulates total power.
// Switching power is counted only on actual 0->1 or 1->0 transitions.
double STA::simulate(int pattern_index) {
    double total_power = 0.0;

    for (auto& [k, net] : Nets) net->Logic_Value = UNSET;
    for (auto& [name, val] : Patterns[pattern_index])
        Nets[name]->Logic_Value = val;

    for (Cell* cell : Cells_In_Topological_Order) {
        const string& lib = cell->Lib_Cell_Name;
        bool new_logic;

        if      (lib.rfind("NOR",  0) == 0)
            new_logic = !(cell->Input_Nets[0]->Logic_Value || cell->Input_Nets[1]->Logic_Value);
        else if (lib.rfind("INV",  0) == 0)
            new_logic = !cell->Input_Nets[0]->Logic_Value;
        else if (lib.rfind("NAND", 0) == 0)
            new_logic = !(cell->Input_Nets[0]->Logic_Value && cell->Input_Nets[1]->Logic_Value);
        else { cerr << "Unsupported cell in simulate: " << lib << "\n"; abort(); }

        cell->Output_Net->Logic_Value = (int)new_logic;

        bool rise = false, fall = false;
        if      (cell->Toggle.Last_Logic_Value == LOW  &&  new_logic)
            { rise = true; cell->Toggle.Last_Logic_Value = HIGH; cell->Toggle.Positive_Toggle++; }
        else if (cell->Toggle.Last_Logic_Value == HIGH && !new_logic)
            { fall = true; cell->Toggle.Last_Logic_Value = LOW;  cell->Toggle.Negative_Toggle++; }

        Set_Cell_Input_Transition_Time(cell, TimingMode::PatternAware);

        const string tblDelay = new_logic ? "cell_rise"       : "cell_fall";
        const string tblTrans = new_logic ? "rise_transition" : "fall_transition";
        const string tblPower = new_logic ? "rise_power"      : "fall_power";

        cell->Timing.Propagation_Delay      = Table_Look_Up(cell, tblDelay);
        cell->Timing.Output_Transition_Time  = Table_Look_Up(cell, tblTrans);
        cell->Power.Internal_Power           = Table_Look_Up(cell, tblPower);
        cell->Power.Switching_Power          = 0.5 * cell->Timing.Output_Loading * VDD * VDD;

        total_power += cell->Power.Internal_Power;
        if (rise || fall) total_power += cell->Power.Switching_Power;
    }
    return total_power;
}

// assign_pattern (Step 4)
// Runs simulate() for each pattern and writes gate_info.txt, gate_power.txt, coverage.txt.
void STA::assign_pattern() {
    ofstream fout_info (Lib_Name + "_" + Mod_Name + "_gate_info.txt");
    ofstream fout_power(Lib_Name + "_" + Mod_Name + "_gate_power.txt");
    ofstream fout_cov  (Lib_Name + "_" + Mod_Name + "_coverage.txt");

    for (size_t i = 0; i < Patterns.size(); ++i) {
        double total = simulate((int)i);
        dump_gate_info (fout_info,  SortKey::InstanceNumber);
        dump_gate_power(fout_power, SortKey::InstanceNumber);
        dump_coverage  (fout_cov, total, (int)i, SortKey::InstanceNumber);
    }
}

// getSortedCells
// Returns all cells sorted by key; ties broken by instance number.
vector<Cell*> STA::getSortedCells(SortKey key) const {
    vector<Cell*> sorted;
    sorted.reserve(Cells.size());
    for (auto& [k, v] : Cells) sorted.push_back(v);
    sort(sorted.begin(), sorted.end(),
         [&](const Cell* a, const Cell* b){ return compareCells(a, b, key, true); });
    return sorted;
}

bool STA::compareCells(const Cell* c1, const Cell* c2, SortKey key, bool asc) const {
    auto tiebreak = [](const Cell* a, const Cell* b) {
        return a->Instance_Number < b->Instance_Number;
    };
    switch (key) {
    case SortKey::InstanceNumber:
        return asc ? c1->Instance_Number < c2->Instance_Number
                   : c1->Instance_Number > c2->Instance_Number;
    case SortKey::PropagationDelay:
        if (c1->Timing.Propagation_Delay == c2->Timing.Propagation_Delay) return tiebreak(c1, c2);
        return asc ? c1->Timing.Propagation_Delay < c2->Timing.Propagation_Delay
                   : c1->Timing.Propagation_Delay > c2->Timing.Propagation_Delay;
    case SortKey::OutputLoading:
        if (c1->Timing.Output_Loading == c2->Timing.Output_Loading) return tiebreak(c1, c2);
        return asc ? c1->Timing.Output_Loading < c2->Timing.Output_Loading
                   : c1->Timing.Output_Loading > c2->Timing.Output_Loading;
    case SortKey::InternalPower:
        if (c1->Power.Internal_Power == c2->Power.Internal_Power) return tiebreak(c1, c2);
        return asc ? c1->Power.Internal_Power < c2->Power.Internal_Power
                   : c1->Power.Internal_Power > c2->Power.Internal_Power;
    case SortKey::SwitchingPower:
        if (c1->Power.Switching_Power == c2->Power.Switching_Power) return tiebreak(c1, c2);
        return asc ? c1->Power.Switching_Power < c2->Power.Switching_Power
                   : c1->Power.Switching_Power > c2->Power.Switching_Power;
    }
    return false;
}

// Dump_Output_Loading - writes load.txt
void STA::Dump_Output_Loading(SortKey key) {
    ofstream fout(Lib_Name + "_" + Mod_Name + "_load.txt");
    for (Cell* cell : getSortedCells(key))
        fout << cell->Name << " "
             << fixed << setprecision(6) << cell->Timing.Output_Loading << "\n";
}

// Dump_Cell_Delay - writes delay.txt
void STA::Dump_Cell_Delay(SortKey key) {
    ofstream fout(Lib_Name + "_" + Mod_Name + "_delay.txt");
    for (Cell* cell : getSortedCells(key))
        fout << cell->Name << " " << cell->Timing.Worst_Case_Output
             << " " << fixed << setprecision(6)
             << cell->Timing.Propagation_Delay << " "
             << cell->Timing.Output_Transition_Time << "\n";
}

// Dump_Longest_And_Shortest_Delay_And_Path - writes path.txt
void STA::Dump_Longest_And_Shortest_Delay_And_Path() {
    ofstream fout(Lib_Name + "_" + Mod_Name + "_path.txt");
    auto printPath = [&](const string& label, double delay, const vector<Net*>& path) {
        fout << label << fixed << setprecision(6) << delay << ", the path is: ";
        for (size_t i = 0; i < path.size(); ++i) {
            fout << path[i]->Name;
            if (i + 1 < path.size()) fout << " -> ";
        }
        fout << "\n";
    };
    printPath("Longest delay = ",  maxDelay, Longest_Path);
    printPath("Shortest delay = ", minDelay, Shortest_Path);
}

void STA::dump_gate_info(ofstream& fout, SortKey key) {
    for (Cell* cell : getSortedCells(key))
        fout << cell->Name << " "
             << cell->Output_Net->Logic_Value << " "
             << fixed << setprecision(6)
             << cell->Timing.Propagation_Delay << " "
             << cell->Timing.Output_Transition_Time << "\n";
    fout << "\n";
}

void STA::dump_gate_power(ofstream& fout, SortKey key) {
    for (Cell* cell : getSortedCells(key))
        fout << cell->Name << " "
             << fixed << setprecision(6)
             << cell->Power.Internal_Power  << " "
             << cell->Power.Switching_Power << "\n";
    fout << "\n";
}

void STA::dump_coverage(ofstream& fout, double total_power, int pattern_index, SortKey key) {
    int total_toggle = 0;
    for (Cell* cell : getSortedCells(key)) {
        total_toggle += min(cell->Toggle.Positive_Toggle, 20);
        total_toggle += min(cell->Toggle.Negative_Toggle, 20);
    }
    fout << pattern_index + 1 << " "
         << fixed << setprecision(6) << total_power << " "
         << setprecision(2) << 100.0 * total_toggle / (40.0 * Num_Cells) << "%\n\n";
}