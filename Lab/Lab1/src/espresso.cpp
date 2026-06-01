#include "espresso.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

using namespace std;

#ifdef VERBOSE
#  define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#  define LOG(...) ((void)0)
#endif

// ==========================================================
// Stop if best_lits has not improved for this many seconds.
// Measured from the last time best_lits was updated, not from start.
// Set to 0.0 to disable and run until time_limit.
static constexpr double EARLY_STOP_SECS = 30.0;

// Threshold for enumeration vs sub_count scan in CoverState operations.
// ndc <= MAX_ENUM_DC -> enumerate 2^ndc minterms (fast for tight cubes).
// ndc >  MAX_ENUM_DC -> scan sub_count keys via containment (fast when |ON| small).
static constexpr int MAX_ENUM_DC = 16;
// ==========================================================

static auto g_start = chrono::steady_clock::now();
static double elapsed() {return chrono::duration<double>(chrono::steady_clock::now() - g_start).count();}

// cube_alg::tautology
// Recursive Shannon expansion: F covers {0,1}^n_bit iff both cofactors do.
namespace cube_alg {
bool tautology(vector<Cube> F, int n_bit) {
    F.erase(remove_if(F.begin(),F.end(),empty),F.end());
    if (F.empty()) return false;
    if (n_bit==0)  return true;
    uint32_t univ = (n_bit >= 32) ? ~0u : (1u<<n_bit)-1;
    for (auto& c:F) if (c.dc==univ) return true;  // universe cube -> trivially true
    int b=0;
    for (int i=0;i<n_bit;i++)
        for (auto& c:F) if (!(c.dc&(1u<<i))) { b=i; goto found; }
    found:
    return tautology(cofactor(F,b,0),n_bit-1) &&
           tautology(cofactor(F,b,1),n_bit-1);
}
} // namespace cube_alg

// ==========================================================
// CoverState tracking logic for minterm-level heuristics.
// Holds sub_count[m], the number of cover cubes overlapping ON-minterm m.
// Maintained incrementally across all phases without expensive full rebuilds.
// ==========================================================
void CoverState::adjust(const Cube& c, int delta) {
    int ndc = __builtin_popcount(c.dc);
    if (ndc <= MAX_ENUM_DC) {
        // Small Cubes (ndc <= MAX_ENUM_DC): Enumerate 2^ndc minterms explicitly. [O(2^ndc)]
        vector<int> fp; fp.reserve(ndc);
        uint32_t tmp = c.dc;
        while (tmp) { fp.push_back(__builtin_ctz(tmp)); tmp &= tmp-1; }
        int total = 1 << (int)fp.size();
        for (int i = 0; i < total; i++) {
            uint32_t m = c.on;
            for (int j = 0; j < (int)fp.size(); j++) if (i & (1<<j)) m |= (1u<<fp[j]);
            auto it = sub_count.find(m);
            if (it != sub_count.end()) it->second += delta;
        }
    } else {
        // Large Cubes (ndc > MAX_ENUM_DC): Scan existing keys via containment. [O(|ON|)]
        uint32_t fmask = ~c.dc, fon = c.on & fmask;
        for (auto& [m,v] : sub_count) if ((m & fmask) == fon) v += delta;
    }
}

// ==================================================
// Find ON minterms covered exclusively by c (sub_count == 1).
// Used in Reduce phase to find essential shrinkage boundaries.
// - out: list of exclusive minterms.
// - fb : fallback minterm (any covered ON minterm).
// Returns true if !out.empty().
// ==================================================
bool CoverState::exclusive_minterms(const Cube& c,
                                     vector<uint32_t>& out,
                                     uint32_t& fb) const {
    out.clear(); fb = UINT32_MAX;
    int ndc = __builtin_popcount(c.dc);
    if (ndc <= MAX_ENUM_DC) {
        vector<int> fp; fp.reserve(ndc);
        uint32_t tmp = c.dc;
        while (tmp) { fp.push_back(__builtin_ctz(tmp)); tmp &= tmp-1; }
        int total = 1 << (int)fp.size();
        for (int i = 0; i < total; i++) {
            uint32_t m = c.on;
            for (int j = 0; j < (int)fp.size(); j++) if (i & (1<<j)) m |= (1u<<fp[j]);
            auto it = sub_count.find(m);
            if (it == sub_count.end()) continue;
            if (fb == UINT32_MAX) fb = m;
            if (it->second == 1) out.push_back(m);
        }
    } else {
        uint32_t fmask = ~c.dc, fon = c.on & fmask;
        for (auto& [m,v] : sub_count) {
            if ((m & fmask) != fon) continue;
            if (fb == UINT32_MAX) fb = m;
            if (v == 1) out.push_back(m);
        }
    }
    return !out.empty();
}

// Returns true if c is redundant: every ON minterm it covers has sub_count >= 2,
// meaning c can be safely removed without uncovering any minterm.
bool CoverState::is_redundant(const Cube& c) const {
    int ndc = __builtin_popcount(c.dc);
    if (ndc <= MAX_ENUM_DC) {
        vector<int> fp; fp.reserve(ndc);
        uint32_t tmp = c.dc;
        while (tmp) { fp.push_back(__builtin_ctz(tmp)); tmp &= tmp-1; }
        int total = 1 << (int)fp.size();
        for (int i = 0; i < total; i++) {
            uint32_t m = c.on;
            for (int j = 0; j < (int)fp.size(); j++) if (i & (1<<j)) m |= (1u<<fp[j]);
            auto it = sub_count.find(m);
            if (it != sub_count.end() && it->second <= 1) return false;
        }
    } else {
        uint32_t fmask = ~c.dc, fon = c.on & fmask;
        for (auto& [m,v] : sub_count)
            if ((m & fmask) == fon && v <= 1) return false;
    }
    return true;
}

// I/O
// Parse input file: n_bit, ON minterms, DC minterms.
// Builds safe_hash = ON ∪ DC for the OFF oracle (hits_off).
void Espresso::parse(const string& inFile) {
    ifstream fin(inFile);
    string line;
    getline(fin,line); n_bit = stoi(line);
    full_mask = (n_bit >= 32) ? ~0u : n_bit ? (1u<<n_bit)-1 : 0;
    getline(fin,line);
    { istringstream ss(line); uint32_t m; while(ss>>m) on_set.push_back({m,0}); }
    getline(fin,line);
    { istringstream ss(line); uint32_t m; while(ss>>m) dc_set.push_back({m,0}); }

    safe_hash.clear();
    safe_hash.reserve(on_set.size() + dc_set.size());
    for (auto& c:on_set) safe_hash.insert(c.on);
    for (auto& c:dc_set) safe_hash.insert(c.on);

    uint32_t total = (n_bit >= 32) ? ~0u : (1u << n_bit);
    uint32_t off_size = total - (uint32_t)safe_hash.size();
    cout << "[ESPRESSO] n=" << n_bit
         << " on=" << on_set.size()
         << " dc=" << dc_set.size()
         << " off=" << off_size
         << " safe=" << safe_hash.size() << endl;
}

// Write SOP cover: each cube as a string of '0', '1', '-' (MSB first).
void Espresso::write_cover(const vector<Cube>& cv, const string& outFile) const {
    ofstream fout(outFile);
    if (!fout) return;
    if (n_bit == 0) { fout << "\n"; return; }
    for (auto& c : cv) {
        for (int b = n_bit-1; b >= 0; b--) {
            uint32_t bit = 1u << b;
            if      (c.dc & bit) fout << '-';
            else if (c.on & bit) fout << '1';
            else                 fout << '0';
        }
        fout << '\n';
    }
}

// Utility

// Total literal count = sum of fixed bits across all cubes.
int Espresso::lit_count(const vector<Cube>& cv) const {
    int n=0; for (auto& c:cv) n += n_bit - __builtin_popcount(c.dc); return n;
}

// Remove duplicate cubes; update sub_count decrementally for removed entries.
void Espresso::dedup() {
    CubeSet seen;
    vector<CoverEntry> r; r.reserve(cover.size());
    for (auto& e : cover) {
        if (seen.insert(e.cube).second) {
            r.push_back(e);
        } else {
            state.remove(e.cube);  // keep sub_count consistent
        }
    }
    LOG("[%.1fs] dedup: %zu -> %zu\n", elapsed(), cover.size(), r.size());
    cover = move(r);
}

// Full rebuild of sub_count from current cover. Called after restore_best.
void Espresso::rebuild_state() {
    state.init(on_set);
    for (auto& e:cover) state.add(e.cube);
}

// Save cover as best if it improves on best_lits; write to output file.
void Espresso::try_save() {
    vector<Cube> cubes; cubes.reserve(cover.size());
    for (auto& e:cover) cubes.push_back(e.cube);
    int lits = lit_count(cubes);
    if (lits < best_lits) {
        best_lits = lits; best = cubes;
        LOG("[%.1fs] BEST %zu cubes, %d lits\n", elapsed(), cover.size(), lits);
        write_cover(cubes, out_file.c_str());
    }
}

// Restore cover from best; mark all cubes dirty so expand re-processes them.
void Espresso::restore_best() {
    cover.clear(); cover.reserve(best.size());
    for (auto& c:best) cover.push_back({c, true});
    rebuild_state();
}

// ==================================================
// OFF oracle: Check if cube 'c' intersects the OFF-set.
// Verified via safe_hash (ON + DC); c hits OFF iff any minterm ∉ safe_hash.
// - ndc <= MAX_ENUM_DC: Forward scan (enumerate 2^ndc minterms, early exit on first miss).
// - ndc >  MAX_ENUM_DC: Reverse scan (count matches in safe_hash, safe if count == 2^ndc).
// ==================================================
bool Espresso::hits_off(const Cube& c) const {
    if (c.dc==0) return !safe_hash.count(c.on);
    int ndc = __builtin_popcount(c.dc);
    if (ndc <= MAX_ENUM_DC) {
        vector<int> fp; fp.reserve(ndc);
        uint32_t tmp = c.dc;
        while (tmp) { fp.push_back(__builtin_ctz(tmp)); tmp &= tmp-1; }
        int total = 1 << (int)fp.size();
        for (int i=0; i<total; i++) {
            uint32_t m = c.on;
            for (int j=0; j<(int)fp.size(); j++) if (i&(1<<j)) m |= (1u<<fp[j]);
            if (!safe_hash.count(m)) return true;
        }
        return false;
    }
    // Reverse scan: need = 2^ndc. Guard: ndc < 63 to avoid UB on 1LL<<ndc.
    uint32_t fmask = ~c.dc & full_mask, fon = c.on & fmask;
    long long need = (ndc < 63) ? (1LL << ndc) : LLONG_MAX;
    long long found = 0;
    for (uint32_t m : safe_hash)
        if ((m & fmask) == fon && ++found == need) return false;
    return true;
}

// ==================================================
// Phase 1: Expand
// - expand_one: Greedily expands one bit at a time. Candidate is accepted 
//               iff it does not intersect the OFF-set (via hits_off).
// - expand_all: Expands all dirty cubes (shrunk in previous Reduce phase).
//               Skips non-dirty cubes to prevent redundant work.
//               Aborts early if the 'tmax' wall-clock deadline is exceeded.
// ==================================================
Cube Espresso::expand_one(Cube c, const vector<int>& order) const {
    for (int b:order) {
        uint32_t bit = 1u<<b;
        if (c.dc&bit) continue;
        Cube cand = {c.on&~bit, c.dc|bit};
        if (!hits_off(cand)) c = cand;
    }
    return c;
}

void Espresso::expand_all(const vector<int>& order, double tmax) {
    int processed = 0;
    for (auto& e : cover) {
        if (elapsed() >= tmax) break;
        if (!e.dirty) continue;
        Cube nc = expand_one(e.cube, order);
        if (nc.on != e.cube.on || nc.dc != e.cube.dc) {
            state.remove(e.cube);
            e.cube = nc;
            state.add(e.cube);
        }
        e.dirty = false;
        processed++;
    }
    LOG("[%.1fs] expand: %d/%zu cubes\n", elapsed(), processed, cover.size());
}

// ==================================================
// Phase 2: Reduce
// - reduce_one: Shrinks cube 'c' toward its exclusive minterms by fixing DC bits
//               to values agreed upon by 'excl'. Collapses to a unit cube if empty.
// - reduce_phase: Reduces all cubes sequentially. Marked changed cubes dirty for Expand.
//                 Processing order (large DC first) maximizes shrinkage flexibility.
// ==================================================
Cube Espresso::reduce_one(const Cube& c) const {
    vector<uint32_t> excl; uint32_t fb;
    state.exclusive_minterms(c, excl, fb);
    if (excl.empty()) return (fb==UINT32_MAX) ? c : Cube{fb,0};
    Cube r = c;
    for (int b=n_bit-1; b>=0; b--) {
        uint32_t bit = 1u<<b;
        if (!(r.dc&bit)) continue;
        bool has0=false, has1=false;
        for (uint32_t m:excl) {
            if (m&bit) has1=true; else has0=true;
            if (has0&&has1) break;
        }
        if      (!has0) r = {(r.on&~bit)|bit, r.dc&~bit};
        else if (!has1) r = {(r.on&~bit),     r.dc&~bit};
    }
    return r;
}

int Espresso::reduce_phase(double tmax) {
    int shrunk = 0;
    for (auto& e : cover) {
        if (elapsed() >= tmax) break;
        e.dirty = false;
        Cube nc = reduce_one(e.cube);
        if (nc.on != e.cube.on || nc.dc != e.cube.dc) {
            state.remove(e.cube);
            e.cube = nc;
            state.add(e.cube);
            e.dirty = true;
            shrunk++;
        }
    }
    LOG("[%.1fs] reduce: %d/%zu shrunk\n", elapsed(), shrunk, cover.size());
    return shrunk;
}

// ==================================================
// Phase 3: Irredundant
// Removes redundant cubes whose covered ON-minterms are already fully covered 
// by others (sub_count > 1). Shuffles first to randomize tie-breaking, then 
// stable_sorts by DC count so larger cubes are evaluated for removal first.
// ==================================================
int Espresso::irredundant_phase(mt19937& rng) {
    shuffle(cover.begin(), cover.end(), rng);
    stable_sort(cover.begin(), cover.end(), [](const CoverEntry& a, const CoverEntry& b) {
        return __builtin_popcount(a.cube.dc) > __builtin_popcount(b.cube.dc);
    });
    vector<CoverEntry> keep; keep.reserve(cover.size());
    for (auto& e : cover) {
        if (state.is_redundant(e.cube)) state.remove(e.cube);
        else                            keep.push_back(e);
    }
    int removed = (int)cover.size() - (int)keep.size();
    cover = move(keep);
    LOG("[%.1fs] irred: %d removed, %zu remain\n", elapsed(), removed, cover.size());
    return removed;
}

// Subsume Pass
// Removes cube c if a 1-bit larger superset b (b ⊇ c) exists in the cover.
// Efficient structural dominance check, sufficient right after Expand.
void Espresso::subsume_pass() {
    CubeSet cs; for (auto& e:cover) cs.insert(e.cube);
    vector<CoverEntry> r; r.reserve(cover.size());
    for (auto& e : cover) {
        bool sub = false;
        uint32_t tmp = full_mask & ~e.cube.dc;
        while (tmp && !sub) {
            int b = __builtin_ctz(tmp); tmp &= tmp-1;
            if (cs.count({e.cube.on&~(1u<<b), e.cube.dc|(1u<<b)})) sub = true;
        }
        if (sub) state.remove(e.cube);
        else     r.push_back(e);
    }
    cover = move(r);
}

// Seed Cover
// Seeds any ON-minterms left uncovered after QMC as unit cubes.
// Establishes the foundational base invariant: cover ⊇ ON-set.
void Espresso::seed_cover() {
    unordered_set<uint32_t> covered; covered.reserve(on_set.size());
    for (auto& e : cover) {
        int ndc = __builtin_popcount(e.cube.dc);
        if (ndc <= MAX_ENUM_DC) {
            vector<int> fp; fp.reserve(ndc);
            uint32_t tmp = e.cube.dc;
            while (tmp) { fp.push_back(__builtin_ctz(tmp)); tmp &= tmp-1; }
            int total = 1 << (int)fp.size();
            for (int i=0; i<total; i++) {
                uint32_t m = e.cube.on;
                for (int j=0; j<(int)fp.size(); j++) if (i&(1<<j)) m |= (1u<<fp[j]);
                covered.insert(m);
            }
        } else {
            uint32_t fmask = ~e.cube.dc, fon = e.cube.on & fmask;
            for (auto& on:on_set) if ((on.on&fmask)==fon) covered.insert(on.on);
        }
    }
    for (auto& on : on_set)
        if (!covered.count(on.on)) { cover.push_back({on, false}); state.add(on); }
}


// QMC Consensus Round
// Performs one round of Quine-McCluskey distance-1 merging.
// Pairs differing by exactly 1 bit are merged via 64-bit hash grouping.
// Returns false when no further merges occur (convergence reached).
bool Espresso::consensus_round(vector<Cube>& cubes) {
    int n = cubes.size();
    vector<bool> merged(n,false); vector<Cube> nc;
    for (int b=0; b<n_bit; b++) {
        uint32_t bit = 1u<<b;
        unordered_map<uint64_t,int> grp; grp.reserve(n);
        for (int i=0; i<n; i++) {
            if (merged[i]||(cubes[i].dc&bit)) continue;
            uint64_t k = ((uint64_t)cubes[i].dc<<32)|(cubes[i].on&~bit);
            auto it = grp.find(k);
            if (it!=grp.end()&&!merged[it->second]) {
                merged[i]=merged[it->second]=true;
                nc.push_back({cubes[i].on&~bit, cubes[i].dc|bit});
                grp.erase(it);
            } else grp[k]=i;
        }
    }
    if (nc.empty()) return false;
    vector<Cube> next;
    for (int i=0;i<n;i++) if (!merged[i]) next.push_back(cubes[i]);
    for (auto& c:nc) next.push_back(c);
    CubeSet seen; cubes.clear();
    for (auto& c:next) if (seen.insert(c).second) cubes.push_back(c);
    return true;
}

// Pipeline
void Espresso::run(double time_limit) {
    if (n_bit==0) { cover.push_back({{0,0},false}); return; }
    if (on_set.empty()) return;

    // 1. QMC: merge unit cubes into larger cubes via distance-1 consensus.
    //    Runs up to 30s; stops when no more merges are possible.
    vector<Cube> cubes = on_set;
    for (int r=0; elapsed()<30.0; r++) {
        if (!consensus_round(cubes)) break;
        LOG("[%.1fs] qmc %d\n", elapsed(), r);
    }
    cover.clear(); cover.reserve(cubes.size());
    for (auto& c:cubes) cover.push_back({c, false});
    dedup();

    // 2. Seed: initialise sub_count and fill any uncovered ON minterms.
    state.init(on_set);
    for (auto& e:cover) state.add(e.cube);
    seed_cover();
    LOG("[%.1fs] post-QMC: %zu cubes\n", elapsed(), cover.size());

    // 3. Initial full expand: maximise all cubes, then remove subsumed ones.
    for (auto& e:cover) e.dirty = true;
    vector<int> order(n_bit); iota(order.begin(),order.end(),0);
    reverse(order.begin(), order.end());
    expand_all(order, time_limit);
    dedup(); subsume_pass();
    try_save();
    for (auto& e:cover) e.dirty = false;

    // ==================================================
    // Phase 4: Main Loop (Reduce -> Irredundant -> Expand)
    // Iteratively optimizes the cover until convergence, stall, or timeout.
    // Stopping conditions:
    //   (a) Structural fixed point: Cover is completely unchanged.
    //   (a2) Extreme rigid structure: Futile to optimize rigid/checkerboard covers.
    //   (b) Stall timeout: No literal improvement for EARLY_STOP_SECS.
    //   (c) Hard wall-clock timeout.
    // ==================================================
    mt19937 rng(chrono::steady_clock::now().time_since_epoch().count());
    double last_improve_t = elapsed();

    for (int iter=0; elapsed()<time_limit-3.0; iter++) {
        double tmax = min(time_limit-3.0, elapsed()+time_limit/30.0);

        int shrunk  = reduce_phase(tmax);
        int removed = irredundant_phase(rng);

        // (a) Structural fixed point
        if (shrunk == 0 && removed == 0) {
            LOG("[%.1fs] structural fixed point -- stop (iter=%d)\n", elapsed(), iter);
            break;
        }

        // (a2) Extreme rigid structure: nearly all cubes essential, expand futile
        if (iter == 0 && removed == 0 &&
                shrunk >= (int)cover.size() * 99 / 100) {
            LOG("[%.1fs] extreme rigid structure -- stop (iter=%d, shrunk=%d/%zu)\n",
                elapsed(), iter, shrunk, cover.size());
            break;
        }

        shuffle(order.begin(), order.end(), rng);
        // Clamp tmax to last_improve_t + EARLY_STOP_SECS so expand aborts
        // when it has been stalling, avoiding wasted work on large covers.
        double expand_tmax = time_limit - 3.0;
        if (EARLY_STOP_SECS > 0.0)
            expand_tmax = min(expand_tmax, last_improve_t + EARLY_STOP_SECS);
        expand_all(order, expand_tmax);
        dedup(); subsume_pass();

        vector<Cube> cur; cur.reserve(cover.size());
        for (auto& e:cover) cur.push_back(e.cube);
        int cur_lits = lit_count(cur);
        LOG("[%.1fs] iter %d lits=%d best=%d cubes=%zu\n",
            elapsed(), iter, cur_lits, best_lits, cover.size());

        if (cur_lits < best_lits) {
            last_improve_t = elapsed();
            try_save();
        }

        // (b) No improvement for EARLY_STOP_SECS seconds
        double stall = elapsed() - last_improve_t;
        if (EARLY_STOP_SECS > 0.0 && stall > EARLY_STOP_SECS) {
            LOG("[%.1fs] early stop -- no improvement for %.1fs\n",
                elapsed(), stall);
            break;
        }

        // Restore best every ~5 stall-seconds to escape local optima.
        if (stall > 0.0 && (int)(stall) % 5 == 0 && (int)(stall) != (int)(stall - 0.1)) {
            restore_best();
            LOG("[%.1fs] stall=%.1fs -> restore_best\n", elapsed(), stall);
        }
    }
    cout << "[" << fixed << setprecision(1) << elapsed() << "s] FINAL "
         << best.size() << " cubes, " << (best_lits == INT_MAX ? 0 : best_lits) << " lits" << endl;
}

// Entry point
void Espresso::solve(const string& inFile, const string& outFile) {
    g_start   = chrono::steady_clock::now();
    best_lits = INT_MAX;
    out_file  = outFile;
    parse(inFile);
    run(175.0);
    write_cover(best, outFile);
}