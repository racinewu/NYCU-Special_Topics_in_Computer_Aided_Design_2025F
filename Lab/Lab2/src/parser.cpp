#include "parser.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <unordered_set>

using namespace std;

namespace {
    const regex kLibName     (R"(library\s*\(\s*(\w+)\s*\))");
    const regex kFloat       (R"(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)");
    const regex kMultiSpace  (R"(\s+)");
    const regex kNetType     (R"(\b(input|output|wire)\b)");
    const regex kWord        (R"(\w+)");
    const regex kComma       (",");
    const regex kModuleName  (R"(module\s+(\w+))");
}

string Parser::stripBlockComments(const string& src) {
    string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i+1] == '/'))
                ++i;
            i += 2;
        } else {
            out += src[i++];
        }
    }
    return out;
}

string Parser::cleanVerilog(const string& src) {
    string tmp;
    tmp.reserve(src.size());
    bool multiLine = false, singleLine = false;

    for (size_t i = 0; i < src.size(); ++i) {
        if (!multiLine && !singleLine) {
            if (i+1 < src.size() && src[i]=='/' && src[i+1]=='*') { multiLine=true;  ++i; }
            else if (i+1 < src.size() && src[i]=='/' && src[i+1]=='/') { singleLine=true; ++i; }
            else tmp += src[i];
        } else if (multiLine) {
            if (i+1 < src.size() && src[i]=='*' && src[i+1]=='/') { multiLine=false; ++i; }
        } else {
            if (src[i] == '\n') { singleLine = false; tmp += '\n'; }
        }
    }

    istringstream iss(tmp);
    ostringstream oss;
    string line;
    while (getline(iss, line)) {
        string cleaned;
        bool prevSpace = false;
        for (char ch : line) {
            if (isspace((unsigned char)ch)) {
                if (!prevSpace) { cleaned += ' '; prevSpace = true; }
            } else {
                cleaned += ch; prevSpace = false;
            }
        }
        auto s = cleaned.find_first_not_of(' ');
        if (s == string::npos) continue;
        auto e = cleaned.find_last_not_of(' ');
        cleaned = cleaned.substr(s, e - s + 1);
        if (!cleaned.empty())
            oss << cleaned << (cleaned.back() == ';' ? '\n' : ' ');
    }
    return oss.str();
}

string Parser::extractBlock(const string& src, size_t open_brace) {
    int depth = 1;
    size_t i = open_brace + 1;
    for (; i < src.size() && depth > 0; ++i) {
        if      (src[i] == '{') ++depth;
        else if (src[i] == '}') --depth;
    }
    size_t end = (depth == 0) ? (i - 1) : src.size();
    return src.substr(open_brace + 1, end - open_brace - 1);
}

vector<double> Parser::extractFloatList(const string& s) {
    vector<double> vals;
    smatch m;
    string tmp = s;
    while (regex_search(tmp, m, kFloat)) {
        vals.push_back(stod(m.str()));
        tmp = m.suffix().str();
    }
    return vals;
}

vector<double> Parser::parseValues(const string& block) {
    size_t first = block.find('"');
    size_t last  = block.rfind('"');
    if (first == string::npos || last == string::npos || first == last) return {};
    return extractFloatList(block.substr(first + 1, last - first - 1));
}

static string gatePrefix(const string& name) {
    if (name.rfind("NAND", 0) == 0) return "NAND";
    if (name.rfind("NOR",  0) == 0) return "NOR";
    if (name.rfind("INV",  0) == 0) return "INV";
    return "";
}

string Parser::resolveLibCellName(const string& netlist_lib_name, const Library* lib) {
    if (lib->Cell_LUT_Map.count(netlist_lib_name)) return netlist_lib_name;
    string prefix = gatePrefix(netlist_lib_name);
    if (prefix.empty()) return "";
    for (auto& [name, _] : lib->Cell_LUT_Map)
        if (gatePrefix(name) == prefix) return name;
    return "";
}

string Parser::resolvePinName(const string& lib_cell_name,
                               const string& netlist_pin,
                               const unordered_map<string, unordered_map<string,string>>& pin_dirs) {
    auto dit = pin_dirs.find(lib_cell_name);
    if (dit == pin_dirs.end()) return netlist_pin;
    const auto& dirs = dit->second;
    if (dirs.count(netlist_pin)) return netlist_pin;

    vector<string> lib_inputs, lib_outputs;
    for (auto& [pname, dir] : dirs) {
        if (dir == "input") lib_inputs.push_back(pname);
        else                lib_outputs.push_back(pname);
    }
    sort(lib_inputs.begin(),  lib_inputs.end());
    sort(lib_outputs.begin(), lib_outputs.end());

    static const unordered_set<string> kOutputAliases = {"ZN","Z","Y","Q","QN","OUT"};
    if (kOutputAliases.count(netlist_pin) && lib_outputs.size() == 1)
        return lib_outputs[0];

    size_t d = netlist_pin.find_first_of("0123456789");
    if (d != string::npos) {
        int idx = stoi(netlist_pin.substr(d)) - 1;
        if (idx >= 0 && (size_t)idx < lib_inputs.size())
            return lib_inputs[idx];
    }
    if (lib_inputs.size() == 1) return lib_inputs[0];
    return netlist_pin;
}

Library* Parser::parseLibrary(ifstream& stream, string& lib_name_out,
                               unordered_map<string, unordered_map<string,string>>& pin_dirs_out) {
    if (!stream.is_open()) { cerr << "Error: library stream not open.\n"; return nullptr; }

    ostringstream raw;
    string line;
    while (getline(stream, line)) raw << line << '\n';
    string src = stripBlockComments(raw.str());

    smatch m;
    if (regex_search(src, m, kLibName))
        lib_name_out = m[1].str();
    else { cerr << "Cannot find library name.\n"; return nullptr; }

    size_t libBrace = src.find('{');
    if (libBrace == string::npos) { cerr << "No opening brace in lib.\n"; return nullptr; }
    string libBody = extractBlock(src, libBrace);

    auto* library = new Library();

    auto parseTemplates = [&](const string& body, const string& kw) {
        size_t pos = 0;
        while (true) {
            size_t kp = body.find(kw, pos);
            if (kp == string::npos) break;
            size_t nameStart = body.find('(', kp + kw.size());
            size_t nameEnd   = body.find(')', nameStart);
            if (nameStart == string::npos || nameEnd == string::npos) { pos=kp+1; continue; }
            string tmplName = body.substr(nameStart + 1, nameEnd - nameStart - 1);
            tmplName.erase(0, tmplName.find_first_not_of(" \t\n\r"));
            tmplName.erase(tmplName.find_last_not_of(" \t\n\r") + 1);
            size_t brace = body.find('{', nameEnd);
            if (brace == string::npos) { pos=kp+1; continue; }
            string blk = extractBlock(body, brace);

            auto& tmpl = library->Templates[tmplName];
            size_t i1 = blk.find("index_1");
            if (i1 != string::npos) {
                size_t q1 = blk.find('"', i1), q2 = blk.find('"', q1+1);
                if (q1 != string::npos && q2 != string::npos)
                    tmpl.first = extractFloatList(blk.substr(q1+1, q2-q1-1));
            }
            size_t i2 = blk.find("index_2");
            if (i2 != string::npos) {
                size_t q1 = blk.find('"', i2), q2 = blk.find('"', q1+1);
                if (q1 != string::npos && q2 != string::npos)
                    tmpl.second = extractFloatList(blk.substr(q1+1, q2-q1-1));
            }
            pos = brace + 1;
        }
    };
    for (const auto& kw : {"lu_table_template","power_lut_template","power_lut_table_template"})
        parseTemplates(libBody, kw);

    // Table parser lambda
    auto parseTables = [&](const string& pinBody, Cell_LUT* lut) {
        static const vector<string> tableKeys = {
            "rise_power","fall_power",
            "cell_rise","cell_fall",
            "rise_transition","fall_transition"
        };
        for (const auto& tk : tableKeys) {
            size_t pos2 = 0;
            while (true) {
                size_t tp = pinBody.find(tk, pos2);
                if (tp == string::npos) break;
                size_t after = pinBody.find_first_not_of(" \t\n\r", tp + tk.size());
                if (after == string::npos || pinBody[after] != '(') { pos2=tp+1; continue; }
                size_t tEnd = pinBody.find(')', after);
                string tmplName = (tEnd != string::npos) ? pinBody.substr(after+1, tEnd-after-1) : "";
                tmplName.erase(0, tmplName.find_first_not_of(" \t\n\r"));
                tmplName.erase(tmplName.find_last_not_of(" \t\n\r") + 1);
                size_t brace = pinBody.find('{', tEnd);
                if (brace == string::npos) { pos2=tp+1; continue; }
                string tblBlk = extractBlock(pinBody, brace);
                size_t vp = tblBlk.find("values");
                if (vp != string::npos) {
                    size_t endV = tblBlk.find(");", vp);
                    string vBlk = (endV != string::npos) ? tblBlk.substr(vp, endV-vp+2) : tblBlk.substr(vp);
                    auto vals = parseValues(vBlk);
                    if (!vals.empty()) {
                        lut->Look_Up_Tables[tk] = move(vals);
                        lut->Table_Template[tk]  = tmplName;
                    }
                }
                pos2 = brace + 1;
                break;
            }
        }
    };

    // Cell blocks
    size_t cellPos = 0;
    while (true) {
        size_t cp = libBody.find("cell", cellPos);
        if (cp == string::npos) break;
        size_t afterCell = libBody.find_first_not_of(" \t\n\r", cp + 4);
        if (afterCell == string::npos || libBody[afterCell] != '(') { cellPos=cp+1; continue; }
        if (cp > 0 && (isalnum((unsigned char)libBody[cp-1]) || libBody[cp-1]=='_')) { cellPos=cp+1; continue; }

        size_t nameStart = afterCell + 1;
        size_t nameEnd   = libBody.find(')', nameStart);
        if (nameEnd == string::npos) { cellPos=cp+1; continue; }
        string cellName = libBody.substr(nameStart, nameEnd - nameStart);
        cellName.erase(0, cellName.find_first_not_of(" \t\n\r"));
        cellName.erase(cellName.find_last_not_of(" \t\n\r") + 1);

        size_t cellBrace = libBody.find('{', nameEnd);
        if (cellBrace == string::npos) { cellPos=cp+1; continue; }
        string cellBody = extractBlock(libBody, cellBrace);

        auto* lut = new Cell_LUT(cellName);
        library->Cell_LUT_Map[cellName] = lut;

        // Pin blocks
        size_t pinPos = 0;
        while (true) {
            size_t pp = cellBody.find("pin", pinPos);
            if (pp == string::npos) break;
            if (pp > 0 && (isalnum((unsigned char)cellBody[pp-1]) || cellBody[pp-1]=='_')) { pinPos=pp+1; continue; }
            size_t afterPin = cellBody.find_first_not_of(" \t\n\r", pp + 3);
            if (afterPin == string::npos || cellBody[afterPin] != '(') { pinPos=pp+1; continue; }

            size_t pnStart = afterPin + 1;
            size_t pnEnd   = cellBody.find(')', pnStart);
            if (pnEnd == string::npos) { pinPos=pp+1; continue; }
            string pinName = cellBody.substr(pnStart, pnEnd - pnStart);
            pinName.erase(0, pinName.find_first_not_of(" \t\n\r"));
            pinName.erase(pinName.find_last_not_of(" \t\n\r") + 1);

            size_t pinBrace = cellBody.find('{', pnEnd);
            if (pinBrace == string::npos) { pinPos=pp+1; continue; }
            string pinBody = extractBlock(cellBody, pinBrace);

            // direction
            size_t dp = pinBody.find("direction");
            if (dp != string::npos) {
                size_t colon = pinBody.find(':', dp);
                size_t semi  = pinBody.find(';', colon);
                if (colon != string::npos && semi != string::npos) {
                    string dir = pinBody.substr(colon+1, semi-colon-1);
                    dir.erase(0, dir.find_first_not_of(" \t\n\r"));
                    dir.erase(dir.find_last_not_of(" \t\n\r") + 1);
                    pin_dirs_out[cellName][pinName] = dir;
                }
            }
            // capacitance
            size_t capP = pinBody.find("capacitance");
            if (capP != string::npos) {
                size_t colon = pinBody.find(':', capP);
                size_t semi  = pinBody.find(';', colon);
                if (colon != string::npos && semi != string::npos) {
                    string val = pinBody.substr(colon+1, semi-colon-1);
                    try { lut->Pin_Cap[pinName] = stod(val); } catch (...) {}
                }
            }
            parseTables(pinBody, lut);
            pinPos = pinBrace + pinBody.size() + 1;
        }
        cellPos = cellBrace + cellBody.size() + 1;
    }

    return library;
}

ParsedNetlist Parser::parseNetlist(ifstream& stream,
                                    const unordered_map<string, unordered_map<string,string>>& pin_dirs,
                                    const Library* library) {
    ParsedNetlist result;
    if (!stream.is_open()) { cerr << "Error: netlist stream not open.\n"; return result; }

    ostringstream raw;
    string line;
    while (getline(stream, line)) raw << line << '\n';
    string cleanCode = cleanVerilog(raw.str());

    {
        string tmp;
        tmp.reserve(cleanCode.size() + 128);
        for (char ch : cleanCode) {
            tmp += ch;
            if (ch == ';') tmp += '\n';
        }
        cleanCode = move(tmp);
    }

    smatch match;
    stringstream ss(cleanCode);
    Cell_Type cell_type = Cell_Type::NOR2X1;
    Net_Type  net_type  = Net_Type::Wire;

    while (getline(ss, line)) {
        line = regex_replace(line, kMultiSpace, " ");
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
        while (!line.empty() && line.back()  == ' ') line.pop_back();
        if (line.empty()) continue;

        if (line.find("module") != string::npos && result.Mod_Name.empty()) {
            smatch mm;
            if (regex_search(line, mm, kModuleName))
                result.Mod_Name = mm[1].str();
            continue;
        }
        if (line.find("endmodule") != string::npos) continue;

        // Net declarations
        if (regex_search(line, match, kNetType)) {
            const string& kw = match[0].str();
            if      (kw == "input")  net_type = Net_Type::Input;
            else if (kw == "output") net_type = Net_Type::Output;
            else                     net_type = Net_Type::Wire;

            string rest = match.suffix().str();
            while (regex_search(rest, match, kWord)) {
                string net_name = match.str();
                if (!result.Nets.count(net_name))
                    result.Nets[net_name] = new Net(net_name, net_type);
                rest = match.suffix().str();
            }
            continue;
        }

        // Cell instantiation
        if (regex_search(line, match, kWord)) {
            string netlist_lib_name = match.str();

            if      (netlist_lib_name.rfind("NOR",  0) == 0) cell_type = Cell_Type::NOR2X1;
            else if (netlist_lib_name.rfind("NAND", 0) == 0) cell_type = Cell_Type::NANDX1;
            else if (netlist_lib_name.rfind("INV",  0) == 0) cell_type = Cell_Type::INVX1;
            else continue;

            string lib_cell_name = resolveLibCellName(netlist_lib_name, library);
            if (lib_cell_name.empty()) {
                cerr << "Warning: no library cell for: " << netlist_lib_name << "\n";
                continue;
            }

            string rest = match.suffix().str();
            if (!regex_search(rest, match, kWord)) continue;
            string cell_name = match.str();

            Cell* cell = new Cell(cell_name, cell_type, lib_cell_name);
            result.Cells[cell_name] = cell;

            rest = match.suffix().str();
            while (regex_search(rest, match, kWord)) {
                string netlist_pin = match.str();
                rest = match.suffix().str();
                if (!regex_search(rest, match, kWord)) break;
                string net_name = match.str();
                rest = match.suffix().str();

                string pin_name = resolvePinName(lib_cell_name, netlist_pin, pin_dirs);

                auto dit = pin_dirs.find(lib_cell_name);
                if (dit == pin_dirs.end()) continue;
                auto pinIt = dit->second.find(pin_name);
                if (pinIt == dit->second.end()) {
                    cerr << "Warning: pin not found: " << pin_name << " in " << lib_cell_name << "\n";
                    continue;
                }
                auto netIt = result.Nets.find(net_name);
                if (netIt == result.Nets.end()) {
                    cerr << "Warning: net not declared: " << net_name << "\n";
                    continue;
                }
                Net* net = netIt->second;

                if (pinIt->second == "output") {
                    net->Input_Cells = {pin_name, cell};
                    cell->Output_Net = net;
                    if (net->Type == Net_Type::Output) {
                        cell->Timing.Output_Loading = PRIMARY_OUTPUT_LOADING;
                        result.Primary_Output_Cells.push_back(cell);
                    }
                } else {
                    net->Output_Cells.emplace_back(pin_name, cell);
                    cell->Input_Nets.push_back(net);
                }
            }
        }
    }
    return result;
}

ParsedPatterns Parser::parsePatterns(ifstream& stream) {
    ParsedPatterns result;
    if (!stream.is_open()) return result;

    string line;
    if (!getline(stream, line)) return result;
    line = regex_replace(line, kComma, "");
    {
        stringstream ss(line);
        string tok;
        ss >> tok; // skip "input"
        while (ss >> tok) result.Input_Order.push_back(tok);
    }

    while (getline(stream, line)) {
        if (line == ".end") break;
        if (line.empty()) continue;
        stringstream ss(line);
        unordered_map<string, int> pattern;
        for (const auto& name : result.Input_Order) {
            int v;
            if (!(ss >> v)) { cerr << "Warning: short pattern line\n"; break; }
            pattern[name] = v;
        }
        if (pattern.size() == result.Input_Order.size())
            result.Patterns.push_back(move(pattern));
    }
    return result;
}

// Debug Printers
void Parser::printLibrary(const Library* library, const string& lib_name) {
    cout << "=== LIBRARY: " << lib_name << " ===\n";
    for (auto& [name, lut] : library->Cell_LUT_Map) {
        cout << "Cell: " << name << "\n";
        for (auto& [pin, cap] : lut->Pin_Cap)
            cout << "  pin " << pin << " cap=" << cap << "\n";
        for (auto& [tbl, vals] : lut->Look_Up_Tables)
            cout << "  table " << tbl << " size=" << vals.size() << "\n";
    }
    cout << "Templates:\n";
    for (auto& [tname, pr] : library->Templates)
        cout << "  " << tname
             << " idx1=" << pr.first.size()
             << " idx2=" << pr.second.size() << "\n";
}

void Parser::printNetlist(const ParsedNetlist& netlist) {
    cout << "=== NETLIST: " << netlist.Mod_Name
         << " cells=" << netlist.Cells.size()
         << " nets="  << netlist.Nets.size() << " ===\n";
    for (auto& [name, cell] : netlist.Cells) {
        cout << cell->Name << " (" << cell->Lib_Cell_Name << ")\n";
        cout << "  inputs:";
        for (auto* n : cell->Input_Nets) cout << " " << n->Name;
        cout << "  output: " << (cell->Output_Net ? cell->Output_Net->Name : "?") << "\n";
    }
}

void Parser::printPatterns(const ParsedPatterns& patterns) {
    cout << "=== PATTERNS ===\n";
    for (size_t i = 0; i < patterns.Patterns.size(); ++i) {
        cout << "P" << i + 1 << ":";
        for (auto& name : patterns.Input_Order)
            cout << " " << patterns.Patterns[i].at(name);
        cout << "\n";
    }
}