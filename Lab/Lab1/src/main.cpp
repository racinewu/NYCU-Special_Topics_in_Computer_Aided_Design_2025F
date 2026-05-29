#include "espresso.h"
#include <iostream>
#include <string>
#include <fstream>

using namespace std;

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cerr << "Usage: ./sop <spec_file> <out_sop_file>" << endl;
        return 1;
    }

    try
    {
        string inputFile = argv[1];
        string outputFile = argv[2];

        ifstream infile(inputFile);
        if (!infile)
        {
            cerr << "Failed to open input file: " << inputFile << endl;
            return 1;
        }

        ofstream outfile(outputFile);
        if (!outfile)
        {
            cerr << "Failed to open output file: " << outputFile << endl;
            return 1;
        }

        Espresso solver;
        solver.solve(argv[1], argv[2]);

        return 0;
    }
    catch (const exception &e)
    {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
}