#pragma once
#include <climits>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

struct Cube { uint32_t on = 0, dc = 0; };
struct CubeHash { size_t operator()(const Cube& c) const { return (size_t)c.on*2654435761u^(size_t)c.dc*40503u; } };
struct CubeEq   { bool operator()(const Cube& a, const Cube& b) const { return a.on==b.on&&a.dc==b.dc; } };
using CubeSet = std::unordered_set<Cube,CubeHash,CubeEq>;

struct CoverEntry { Cube cube; bool dirty = false; };

namespace cube_alg {
inline bool subset(const Cube& a, const Cube& b) {
    if (a.dc & ~b.dc) return false;
    return (a.on & ~b.dc) == (b.on & ~b.dc);
}
inline Cube intersect(const Cube& a, const Cube& b) {
    uint32_t both = ~a.dc & ~b.dc;
    if ((a.on & both) != (b.on & both)) return {0, ~0u};
    return {(a.on|b.on) & ~(a.dc&b.dc), a.dc&b.dc};
}
inline bool empty(const Cube& c) { return c.dc == ~0u; }
inline std::vector<Cube> sharp(const Cube& a, const Cube& b) {
    if (empty(intersect(a,b))) return {a};
    if (subset(a,b))           return {};
    std::vector<Cube> res; Cube r=a;
    for (int i=0;i<32;i++) {
        uint32_t bit=1u<<i;
        if (b.dc&bit||!(r.dc&bit)) continue;
        res.push_back({(r.on&~bit)|(~b.on&bit), r.dc&~bit});
        r={(r.on&~bit)|(b.on&bit), r.dc&~bit};
    }
    return res;
}
inline std::vector<Cube> cofactor(const std::vector<Cube>& F, int b, int val) {
    uint32_t bit=1u<<b; std::vector<Cube> R;
    for (auto& c:F) {
        if (c.dc&bit) R.push_back({(c.on&~bit)|(val?bit:0u), c.dc&~bit});
        else if (((c.on>>b)&1)==(unsigned)val) R.push_back(c);
    }
    return R;
}
bool tautology(std::vector<Cube> F, int n_bit);
} // namespace cube_alg

struct CoverState {
    std::unordered_map<uint32_t,int> sub_count;
    void init(const std::vector<Cube>& on_set) {
        sub_count.clear(); sub_count.reserve(on_set.size());
        for (auto& e:on_set) sub_count[e.on]=0;
    }
    void adjust(const Cube& c, int delta);
    void add(const Cube& c)    { adjust(c,+1); }
    void remove(const Cube& c) { adjust(c,-1); }
    bool exclusive_minterms(const Cube& c, std::vector<uint32_t>& out, uint32_t& fallback) const;
    bool is_redundant(const Cube& c) const;
};

class Espresso {
public:
    void solve(const std::string& inFile, const std::string& outFile);

private:
    int      n_bit    = 0;
    uint32_t full_mask = 0;
    std::vector<Cube> on_set, dc_set;
    std::unordered_set<uint32_t> safe_hash;

    std::vector<CoverEntry> cover;
    CoverState state;

    std::vector<Cube> best;
    int best_lits = INT_MAX;
    std::string out_file;

    void parse(const std::string& inFile);
    void write_cover(const std::vector<Cube>& cv, const std::string& path) const;

    int  lit_count(const std::vector<Cube>& cv) const;
    void dedup();
    void try_save();
    void rebuild_state();
    void restore_best();

    bool hits_off(const Cube& c) const;

    Cube expand_one(Cube c, const std::vector<int>& order) const;
    void expand_all(const std::vector<int>& order, double tmax);

    Cube reduce_one(const Cube& c) const;
    int  reduce_phase(double tmax);
    int  irredundant_phase(std::mt19937& rng);

    void subsume_pass();
    void seed_cover();
    bool consensus_round(std::vector<Cube>& cubes);
    void run(double time_limit);
};