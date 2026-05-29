#include "espresso.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <iostream>
#include <random>
#include <chrono>

using namespace std;

// forEachCovered: iterate all 2^k minterms of an implicant
template<typename Fn>
static void __attribute__((noinline))
forEachCovered(uint32_t base, uint32_t maskBits, int nBit, Fn &&fn) {
    int freeBits[24], freeCnt = 0;
    uint32_t mb = maskBits;
    while (mb) { freeBits[freeCnt++] = __builtin_ctz(mb); mb &= mb-1; }
    uint32_t n = 1u << freeCnt;
    for (uint32_t combo = 0; combo < n; ++combo) {
        uint32_t m = base;
        for (int i = 0; i < freeCnt; ++i)
            if (combo & (1u<<i)) m |= (1u << freeBits[i]);
        fn(m);
    }
}

static inline uint32_t getBase(const Implicant &imp, uint32_t fullMask) {
    return imp.on & fullMask & ~imp.mask;
}

// I/O
void Espresso::parseSpec(const string &path) {
    ifstream f(path);
    if (!f) throw runtime_error("Cannot open: " + path);
    string line;
    getline(f, line); nBit = stoi(line);
    fullMask      = (nBit == 0) ? 0u : ((1u << nBit) - 1u);
    totalMinterms = (nBit == 0) ? 1u : (1u << nBit);
    offArr.assign(totalMinterms, 1);
    onArr .assign(totalMinterms, 0);
    getline(f, line);
    { istringstream ss(line); uint32_t v;
      while (ss>>v) { onArr[v]=1; offArr[v]=0; onVec.push_back(v); } }
    getline(f, line);
    { istringstream ss(line); uint32_t v; while (ss>>v) offArr[v]=0; }
    onSize = (int)onVec.size();
}

void Espresso::writeOutput(const string &path, const vector<Implicant> &F) {
    ofstream f(path);
    if (!f) throw runtime_error("Cannot open output: " + path);
    if (nBit == 0) { f << "\n"; return; }
    for (auto &imp : F) f << toStr(imp) << "\n";
}

string Espresso::toStr(const Implicant &imp) const {
    string s(nBit, '0');
    for (int i = 0; i < nBit; ++i) {
        int b = nBit - 1 - i;
        if      (imp.mask & (1u<<b)) s[i] = '-';
        else if (imp.on   & (1u<<b)) s[i] = '1';
    }
    return s;
}

int Espresso::countLiterals(const vector<Implicant> &F) const {
    int c = 0;
    for (auto &imp : F)
        c += nBit - __builtin_popcount(imp.mask);
    return c;
}

// expandOne: try a specific bit ordering, return best expansion
__attribute__((noinline))
Implicant Espresso::expandWithOrder(const Implicant &imp,
                                     const int *order) const {
    Implicant cur = imp;
    bool progress = true;
    while (progress) {
        progress = false;
        for (int oi = 0; oi < nBit; ++oi) {
            int b = order[oi];
            if (cur.mask & (1u<<b)) continue;
            uint32_t curBase = getBase(cur, fullMask);
            uint32_t newBase = curBase ^ (1u<<b);
            bool safe = true;
            forEachCovered(newBase, cur.mask, nBit, [&](uint32_t m) {
                if (offArr[m]) safe = false;
            });
            if (safe) {
                cur.mask |= (1u<<b);
                cur.on   &= ~(1u<<b);
                progress = true;
            }
        }
    }
    return cur;
}

// expandOne: try multiple bit orderings, return the one with most don't-cares
__attribute__((noinline))
Implicant Espresso::expandOne(const Implicant &imp) const {
    // Always try MSB→LSB first
    int order0[24] = {};
    for (int i = 0; i < nBit; ++i) order0[i] = nBit-1-i;
    Implicant best = expandWithOrder(imp, order0);
    int bestDC = __builtin_popcount(best.mask);

    // Try LSB→MSB
    int order1[24] = {};
    for (int i = 0; i < nBit; ++i) order1[i] = i;
    Implicant r = expandWithOrder(imp, order1);
    if (__builtin_popcount(r.mask) > bestDC) { best = r; bestDC = __builtin_popcount(r.mask); }

    // Try random orderings (seeded by the implicant value for determinism)
    uint32_t seed = imp.on * 2654435761u;
    int orderR[24] = {};
    for (int trial = 0; trial < numExpandTrials-2; ++trial) {
        for (int i = 0; i < nBit; ++i) orderR[i] = i;
        // Fisher-Yates with LCG
        for (int i = nBit-1; i > 0; --i) {
            seed = seed * 1664525u + 1013904223u;
            int j = seed % (i+1);
            int tmp = orderR[i]; orderR[i] = orderR[j]; orderR[j] = tmp;
        }
        r = expandWithOrder(imp, orderR);
        int dc = __builtin_popcount(r.mask);
        if (dc > bestDC) { best = r; bestDC = dc; }
    }
    return best;
}

void Espresso::expandAll(vector<Implicant> &F) const {
    for (auto &imp : F) imp = expandOne(imp);
}

// irredundant: essential + greedy + redundancy sweep
__attribute__((noinline))
void Espresso::irredundant(vector<Implicant> &F) const {
    if (F.empty()) return;
    int N = (int)F.size();

    // coverCount[m]: how many implicants in F cover m (capped at 2)
    // singleCover[m]: index if exactly 1 implicant covers m
    vector<uint8_t> coverCount(totalMinterms, 0);
    vector<int>     singleCover(totalMinterms, -1);

    for (int i = 0; i < N; ++i) {
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (!onArr[m]) return;
            uint8_t &cc = coverCount[m];
            if (cc == 0) { cc = 1; singleCover[m] = i; }
            else if (cc == 1 && singleCover[m] != i) { cc = 2; }
        });
    }

    // Essential implicants
    vector<bool> essential(N, false);
    for (auto m : onVec)
        if (coverCount[m] == 1) essential[singleCover[m]] = true;

    // Per-implicant on-coverage count (for greedy sorting)
    vector<int> cov(N, 0);
    for (int i = 0; i < N; ++i) {
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m){ cov[i] += onArr[m]; });
    }

    // Sort non-essential by coverage desc
    vector<int> nonEss;
    nonEss.reserve(N);
    for (int i = 0; i < N; ++i)
        if (!essential[i]) nonEss.push_back(i);
    sort(nonEss.begin(), nonEss.end(), [&](int a, int b){ return cov[a] > cov[b]; });

    // Mark minterms covered by essentials
    vector<uint8_t> covered(totalMinterms, 0);
    int remaining = onSize;
    vector<bool> selected = essential;

    for (int i = 0; i < N; ++i) {
        if (!essential[i]) continue;
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (onArr[m] && !covered[m]) { covered[m]=1; --remaining; }
        });
    }

    // Greedy phase
    for (int i : nonEss) {
        if (remaining == 0) break;
        uint32_t base = getBase(F[i], fullMask);
        int newCov = 0;
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (onArr[m] && !covered[m]) ++newCov;
        });
        if (newCov > 0) {
            selected[i] = true;
            forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
                if (onArr[m] && !covered[m]) { covered[m]=1; --remaining; }
            });
        }
    }

    // Safety net: patch any still-uncovered minterms
    if (remaining > 0) {
        for (auto m : onVec) {
            if (covered[m]) continue;
            int i = singleCover[m]; // if ==1, use it; else scan
            if (i >= 0 && i < N) {
                if (!selected[i]) selected[i] = true;
                uint32_t base = getBase(F[i], fullMask);
                forEachCovered(base, F[i].mask, nBit, [&](uint32_t mm) {
                    if (onArr[mm] && !covered[mm]) { covered[mm]=1; --remaining; }
                });
            } else {
                // Scan all implicants for one covering m
                for (int j = 0; j < N; ++j) {
                    uint32_t care = fullMask & ~F[j].mask;
                    if ((m & care) == (F[j].on & care)) {
                        selected[j] = true;
                        uint32_t base = getBase(F[j], fullMask);
                        forEachCovered(base, F[j].mask, nBit, [&](uint32_t mm) {
                            if (onArr[mm] && !covered[mm]) { covered[mm]=1; --remaining; }
                        });
                        break;
                    }
                }
            }
        }
    }

    // Rebuild F
    vector<Implicant> newF;
    newF.reserve(N);
    for (int i = 0; i < N; ++i)
        if (selected[i]) newF.push_back(F[i]);
    F = move(newF);

    // ── Redundancy sweep: remove implicants whose coverage is subsumed ──
    // Re-check: can we drop any selected implicant?
    // Build covered counts again with final selection
    fill(coverCount.begin(), coverCount.end(), 0);
    fill(singleCover.begin(), singleCover.end(), -1);
    N = (int)F.size();
    for (int i = 0; i < N; ++i) {
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (!onArr[m]) return;
            uint8_t &cc = coverCount[m];
            if (cc == 0) { cc = 1; singleCover[m] = i; }
            else if (cc == 1 && singleCover[m] != i) { cc = 2; }
        });
    }
    // An implicant is redundant if it covers NO minterm with coverCount==1
    vector<bool> keep(N, false);
    for (auto m : onVec)
        if (coverCount[m] == 1) keep[singleCover[m]] = true;
    // Greedily remove non-essential ones (those with keep=false)
    // But we must ensure coverage: if we remove one, re-check others
    // Simple pass: mark all keep=true ones, others need coverage check
    {
        vector<uint8_t> covAfter(totalMinterms, 0);
        for (int i = 0; i < N; ++i) {
            if (!keep[i]) continue;
            uint32_t base = getBase(F[i], fullMask);
            forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
                if (onArr[m]) covAfter[m] = 1;
            });
        }
        // Check which non-kept implicants still add coverage
        for (int i = 0; i < N; ++i) {
            if (keep[i]) continue;
            uint32_t base = getBase(F[i], fullMask);
            bool needed = false;
            forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
                if (onArr[m] && !covAfter[m]) needed = true;
            });
            if (needed) {
                keep[i] = true;
                forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
                    if (onArr[m]) covAfter[m] = 1;
                });
            }
        }
    }
    vector<Implicant> newF2;
    newF2.reserve(N);
    for (int i = 0; i < N; ++i)
        if (keep[i]) newF2.push_back(F[i]);
    F = move(newF2);
}

// reduce: shrink implicants for next expand iteration
__attribute__((noinline))
void Espresso::reduce(vector<Implicant> &F) const {
    int N = (int)F.size();
    if (N == 0) return;

    vector<int> coveredBy(totalMinterms, -1);
    for (int i = 0; i < N; ++i) {
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (!onArr[m]) return;
            if      (coveredBy[m] == -1) coveredBy[m] = i;
            else if (coveredBy[m] != i)  coveredBy[m] = N;
        });
    }

    for (int i = 0; i < N; ++i) {
        vector<uint32_t> excl;
        uint32_t base = getBase(F[i], fullMask);
        forEachCovered(base, F[i].mask, nBit, [&](uint32_t m) {
            if (onArr[m] && coveredBy[m] == i) excl.push_back(m);
        });
        if (excl.empty()) continue;

        Implicant imp = F[i];
        for (int b = 0; b < nBit; ++b) {
            if (!(imp.mask & (1u<<b))) continue;
            // Try 0
            Implicant c = imp; c.mask &= ~(1u<<b); c.on &= ~(1u<<b);
            uint32_t cr = fullMask & ~c.mask; bool ok = true;
            for (auto m : excl) if ((m&cr) != (c.on&cr)) { ok=false; break; }
            if (ok) { imp = c; continue; }
            // Try 1
            c = imp; c.mask &= ~(1u<<b); c.on |= (1u<<b);
            cr = fullMask & ~c.mask; ok = true;
            for (auto m : excl) if ((m&cr) != (c.on&cr)) { ok=false; break; }
            if (ok) imp = c;
        }
        F[i] = imp;
    }
}

// verifyAndPatch: guarantee all on-set minterms are covered
void Espresso::verifyAndPatch(vector<Implicant> &F) const {
    vector<uint8_t> chk(totalMinterms, 0);
    for (auto &imp : F) {
        uint32_t base = getBase(imp, fullMask);
        forEachCovered(base, imp.mask, nBit, [&](uint32_t m){ chk[m]=1; });
    }
    for (auto m : onVec)
        if (!chk[m]) F.push_back({m, 0u});
}

vector<Implicant> Espresso::initialCover() const {
    vector<Implicant> F;
    F.reserve(onVec.size());
    for (auto m : onVec) F.push_back({m, 0u});
    return F;
}

// Deduplicate helper
static void dedup(vector<Implicant> &F) {
    sort(F.begin(), F.end());
    F.erase(unique(F.begin(), F.end()), F.end());
}

// solve
void Espresso::solve(const string &specFile, const string &outFile) {
    parseSpec(specFile);
    if (onSize == 0) { writeOutput(outFile, {}); return; }
    if (nBit == 0)   { writeOutput(outFile, {{0,0}}); return; }

    auto wallMs = [](){ 
        return chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count();
    };
    long long t0 = wallMs();
    long long timeLimitMs = 150000; // 2.5 minutes limit (leave margin)

    vector<Implicant> F = initialCover();

    // Estimate trials based on case size
    // Each trial costs ~proportional to onSize
    // Budget: 100 seconds total for first expand pass
    // trial_cost ≈ onSize * nBit * avg_2^freeCnt ≈ onSize * nBit * 4
    // trials = min(16, max(2, 100000ms / estimated_cost_per_trial_ms))
    bool largeCase = (onSize > 200000);
    int estimatedMsPerTrial = (int)((long long)onSize * nBit / 5000); // rough estimate
    numExpandTrials = 16;
    while (numExpandTrials > 2 && numExpandTrials * estimatedMsPerTrial > 80000)
        numExpandTrials /= 2;
    numExpandTrials = max(2, min(16, numExpandTrials));

    expandAll(F);
    dedup(F);
    irredundant(F);
    verifyAndPatch(F);

    int bestLit = countLiterals(F);
    vector<Implicant> bestF = F;

    if (!largeCase) {
        // ── Iterations: reduce → expand → irredundant ──
        const int MAX_ITER = 20;
        for (int iter = 0; iter < MAX_ITER; ++iter) {
            long long elapsed = wallMs() - t0;
            if (elapsed > timeLimitMs) break;
            numExpandTrials = (elapsed < 30000) ? 8 : 4;
            auto prev = F;
            reduce(F);
            expandAll(F);
            dedup(F);
            irredundant(F);
            verifyAndPatch(F);
            dedup(F);
            int lit = countLiterals(F);
            if (lit < bestLit) { bestLit = lit; bestF = F; }
            if (F == prev) break;
        }
    } else {
        // Large case: single expand pass with 2 trials, then greedy iterations
        // Try more trials in second pass if time allows
        const int MAX_ITER = 5;
        for (int iter = 0; iter < MAX_ITER; ++iter) {
            long long elapsed = wallMs() - t0;
            if (elapsed > timeLimitMs) break;
            numExpandTrials = 2;
            auto prev = F;
            reduce(F);
            expandAll(F);
            dedup(F);
            irredundant(F);
            verifyAndPatch(F);
            dedup(F);
            int lit = countLiterals(F);
            if (lit < bestLit) { bestLit = lit; bestF = F; }
            if (F == prev) break;
        }
    }

    dedup(bestF);
    writeOutput(outFile, bestF);
}

bool Espresso::hitsOff(const Implicant &imp) const {
    bool hit = false;
    forEachCovered(getBase(imp,fullMask), imp.mask, nBit, [&](uint32_t m){ if(offArr[m]) hit=true; });
    return hit;
}