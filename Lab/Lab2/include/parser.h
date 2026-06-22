#pragma once

#include "types.h"
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>

struct ParsedNetlist {
    std::string                             Mod_Name;
    std::unordered_map<std::string, Cell*>  Cells;
    std::unordered_map<std::string, Net*>   Nets;
    std::vector<Cell*>                      Primary_Output_Cells;
};

struct ParsedPatterns {
    std::vector<std::string>                           Input_Order;
    std::vector<std::unordered_map<std::string, int>>  Patterns;
};

class Parser {
public:
    static Library*       parseLibrary (std::ifstream& stream,
                                        std::string& lib_name_out,
                                        std::unordered_map<std::string,
                                            std::unordered_map<std::string, std::string>>& pin_dirs_out);
    static ParsedNetlist  parseNetlist (std::ifstream& stream,
                                        const std::unordered_map<std::string,
                                            std::unordered_map<std::string, std::string>>& pin_dirs,
                                        const Library* library);
    static ParsedPatterns parsePatterns(std::ifstream& stream);

    // Debug helpers - print parse results to stdout.
    static void printLibrary (const Library* library, const std::string& lib_name);
    static void printNetlist (const ParsedNetlist& netlist);
    static void printPatterns(const ParsedPatterns& patterns);

private:
    // Comment stripping
    static std::string stripBlockComments(const std::string& src);
    static std::string cleanVerilog      (const std::string& src);

    // Liberty block helpers
    static std::string         extractBlock    (const std::string& src, size_t open_brace);
    static std::vector<double> extractFloatList(const std::string& s);
    static std::vector<double> parseValues     (const std::string& block);

    // Netlist name resolution - handles naming mismatches between netlist and lib.
    // e.g. NANDX1 (netlist) <-> NAND2 (lib),  A1/A2 (netlist) <-> A/B (lib).
    static std::string resolveLibCellName(const std::string& netlist_lib_name,
                                          const Library* library);
    static std::string resolvePinName    (const std::string& lib_cell_name,
                                          const std::string& netlist_pin,
                                          const std::unordered_map<std::string,
                                              std::unordered_map<std::string, std::string>>& pin_dirs);
};