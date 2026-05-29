#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct Implicant {
    uint32_t on;
    uint32_t mask;
    bool operator==(const Implicant &o) const { return on==o.on && mask==o.mask; }
    bool operator< (const Implicant &o) const { return on!=o.on ? on<o.on : mask<o.mask; }
};

class Espresso {
public:
    void solve(const std::string &specFile, const std::string &outFile);

private:
    int      nBit;
    uint32_t fullMask;
    uint32_t totalMinterms;
    int      numExpandTrials = 4; // number of bit orderings to try per implicant

    std::vector<uint8_t> offArr;
    std::vector<uint8_t> onArr;
    std::vector<uint32_t> onVec;
    int onSize;

    void parseSpec(const std::string &path);
    void writeOutput(const std::string &path, const std::vector<Implicant> &F);

    std::vector<Implicant> initialCover() const;
    void expandAll    (std::vector<Implicant> &F) const;
    void irredundant  (std::vector<Implicant> &F) const;
    void reduce       (std::vector<Implicant> &F) const;
    void verifyAndPatch(std::vector<Implicant> &F) const;

    Implicant expandOne      (const Implicant &imp) const;
    Implicant expandWithOrder(const Implicant &imp, const int *order) const;

    bool hitsOff(const Implicant &imp) const;

    int  countLiterals(const std::vector<Implicant> &F) const;
    std::string toStr(const Implicant &imp) const;
};