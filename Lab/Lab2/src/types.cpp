#include "types.h"
#include <regex>
#include <stdexcept>

using namespace std;

Cell::Cell(const string& name, Cell_Type type, const string& lib_cell_name)
    : Name(name), Type(type), Lib_Cell_Name(lib_cell_name)
{
    static const regex num_re(R"(\d+)");
    smatch m;
    if (regex_search(name, m, num_re))
        Instance_Number = stoi(m.str());
    else
        throw invalid_argument("Cell name contains no number: " + name);

    // Controlling value: the input value that alone determines the output.
    //   NOR:  any HIGH input forces output LOW  -> controlling = HIGH (true)
    //   NAND: any LOW  input forces output HIGH -> controlling = LOW  (false)
    //   INV:  single input, field unused
    Controlling_Value = (type == Cell_Type::NOR2X1);
}

Cell::~Cell() = default;

Cell_LUT::Cell_LUT(const string& cell_name) : Cell_Name(cell_name) {}

double Cell_LUT::Get_Pin_Cap(const string& pin_name) const {
    auto it = Pin_Cap.find(pin_name);
    if (it == Pin_Cap.end())
        throw runtime_error("Pin not found: " + pin_name + " in " + Cell_Name);
    return it->second;
}

const vector<double>& Cell_LUT::Get_Look_Up_Table(const string& table_name) const {
    auto it = Look_Up_Tables.find(table_name);
    if (it == Look_Up_Tables.end())
        throw runtime_error("Table not found: " + table_name + " in " + Cell_Name);
    return it->second;
}

Library::~Library() {
    for (auto& [name, lut] : Cell_LUT_Map) delete lut;
}

const vector<double>& Library::Get_Cell_Table(const string& cell_name,
                                               const string& table_name) const {
    auto it = Cell_LUT_Map.find(cell_name);
    if (it == Cell_LUT_Map.end())
        throw runtime_error("Cell not in library: " + cell_name);
    return it->second->Get_Look_Up_Table(table_name);
}

Net::Net(const string& name, Net_Type type) : Name(name), Type(type) {}