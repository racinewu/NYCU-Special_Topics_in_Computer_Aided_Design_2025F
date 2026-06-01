#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <random>
#include <algorithm>
#include <chrono>

using namespace std;

int main() {
    int numVars;
    cout << "Enter number of variables: ";
    cin >> numVars;

    const int maxMinterm = 1 << numVars; // 2^numVars
    int onMin = maxMinterm * 0.3;
    int onMax = maxMinterm * 0.5;
    int dcMin = maxMinterm * 0.05;
    int dcMax = maxMinterm * 0.1;

    mt19937 rng(chrono::steady_clock::now().time_since_epoch().count());
    uniform_int_distribution<int> onDist(onMin, onMax);
    uniform_int_distribution<int> dcDist(dcMin, dcMax);

    int onCount = onDist(rng);
    int dcCount = dcDist(rng);

    set<int> used;
    vector<int> onSet, dcSet;

    // Generate on_set
    while ((int)onSet.size() < onCount) {
        int m = rng() % maxMinterm;
        if (used.insert(m).second) {
            onSet.push_back(m);
        }
    }

    // Generate dc_set
    while ((int)dcSet.size() < dcCount) {
        int m = rng() % maxMinterm;
        if (used.insert(m).second) {
            dcSet.push_back(m);
        }
    }

    sort(onSet.begin(), onSet.end());
    sort(dcSet.begin(), dcSet.end());

    string filename = "testcase/synth_case" + to_string(numVars) + ".txt";
    ofstream fout(filename);

    fout << numVars << "\n";

    for (size_t i = 0; i < onSet.size(); i++) {
        fout << onSet[i];
        if (i + 1 != onSet.size()) fout << " ";
    }
    fout << "\n";

    for (size_t i = 0; i < dcSet.size(); i++) {
        fout << dcSet[i];
        if (i + 1 != dcSet.size()) fout << " ";
    }

    cout << "Testcase written to " << filename << "\n";
    return 0;
}
