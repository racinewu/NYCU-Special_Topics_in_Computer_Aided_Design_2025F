#include <iostream>
#include <string>
#include "STA.h"

using namespace std;

int main(int argc, char* argv[])
{
    if (argc != 6) {
        cerr << "Usage: ./sta <netlist>.v -l <lib_file>.lib -i <patterns>.pat" << endl;
        return 1;
    }

    try {
        string netlistFile = argv[1];
        string libraryFile = argv[3];
        string patternFile = argv[5];

        STA sta;
        sta.analyze(netlistFile, libraryFile, patternFile);
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    return 0;
}