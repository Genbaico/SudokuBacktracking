#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace sudoku {

using Grid = std::array<int, 81>;                 
using Step = std::pair<std::int8_t, std::int8_t>; 
inline int boxOf(int r, int c) { return (r / 3) * 3 + c / 3; }

inline int popcount16(std::uint16_t m) {
    int c = 0;
    while (m) { m &= (std::uint16_t)(m - 1); c++; }
    return c;
}

struct Stats {
    std::uint64_t calls = 0;      
    std::uint64_t backtracks = 0; 
    int maxDepth = 0;             
    double ms = 0;                
    bool aborted = false;         
};

class Solver {
public:
    bool load(const Grid& in) {
        g.fill(0); rowM.fill(0); colM.fill(0); boxM.fill(0);
        for (int i = 0; i < 81; i++) {
            int v = in[i];
            if (v == 0) continue;
            if (v < 1 || v > 9 || !canPlace(i, v)) return false;
            place(i, v);
        }
        return true;
    }

    void setRandom(std::mt19937* r) { rng = r; }
    void setCallLimit(std::uint64_t l) { callLimit = l; }
    void setCancelFlag(const std::atomic<bool>* c) { cancel = c; }
    
    void setTrace(std::vector<Step>* t, std::size_t maxSteps) { trace = t; traceMax = maxSteps; }

    long solveOptimized(long lim = 1) { return run(lim, true); }
    long solveNaive(long lim = 1) { return run(lim, false); }

    const Grid& solution() const { return firstSol; }
    const Stats& stats() const { return st; }

private:
    Grid g{}, firstSol{};
    std::array<std::uint16_t, 9> rowM{}, colM{}, boxM{};
    Stats st;
    long limit = 1, found = 0;
    std::uint64_t callLimit = 0;
    std::mt19937* rng = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    std::vector<Step>* trace = nullptr;
    std::size_t traceMax = 0;

    bool canPlace(int i, int v) const {
        std::uint16_t b = (std::uint16_t)(1u << (v - 1));
        int r = i / 9, c = i % 9;
        return !((rowM[r] | colM[c] | boxM[boxOf(r, c)]) & b);
    }
    void place(int i, int v) {
        std::uint16_t b = (std::uint16_t)(1u << (v - 1));
        int r = i / 9, c = i % 9;
        g[i] = v; rowM[r] |= b; colM[c] |= b; boxM[boxOf(r, c)] |= b;
    }
    void unplace(int i) {
        std::uint16_t b = (std::uint16_t)~(1u << (g[i] - 1));
        int r = i / 9, c = i % 9;
        rowM[r] &= b; colM[c] &= b; boxM[boxOf(r, c)] &= b; g[i] = 0;
    }
    void log(int i, int v) {
        if (trace && trace->size() < traceMax) trace->emplace_back((std::int8_t)i, (std::int8_t)v);
    }

    long run(long lim, bool optimized) {
        limit = lim; found = 0; st = Stats{};
        Grid gb = g; auto rb = rowM, cb = colM, bb = boxM;
        auto t0 = std::chrono::steady_clock::now();
        if (optimized) dfsMRV(0); else dfsNaive(0, 0);
        st.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        g = gb; rowM = rb; colM = cb; boxM = bb;
        return found;
    }

    bool stop() const { return found >= limit || st.aborted; }

    void enter(int depth) {
        st.calls++;
        if (depth > st.maxDepth) st.maxDepth = depth;
        if (callLimit && st.calls >= callLimit) st.aborted = true;
        if (cancel && (st.calls & 0xFFF) == 0 && cancel->load(std::memory_order_relaxed)) st.aborted = true;
    }
    void record() { if (found == 0) firstSol = g; found++; }

    // ---------------- Solver 1: ingênuo ----------------
    bool isSafeScan(int i, int v) const {
        int r = i / 9, c = i % 9;
        for (int k = 0; k < 9; k++)
            if (g[r * 9 + k] == v || g[k * 9 + c] == v) return false;
        int br = (r / 3) * 3, bc = (c / 3) * 3;
        for (int dr = 0; dr < 3; dr++)
            for (int dc = 0; dc < 3; dc++)
                if (g[(br + dr) * 9 + bc + dc] == v) return false;
        return true;
    }
    void dfsNaive(int pos, int depth) {
        enter(depth);
        if (st.aborted) return;
        while (pos < 81 && g[pos]) pos++;
        if (pos == 81) { record(); return; }
        for (int v = 1; v <= 9; v++) {
            if (!isSafeScan(pos, v)) continue;
            g[pos] = v; log(pos, v);          // escolhe
            dfsNaive(pos + 1, depth + 1);     // explora
            g[pos] = 0;                       // desfaz
            if (stop()) return;
            log(pos, 0);
            st.backtracks++;
        }
    }

    // ---------------- Solver 2: bitmask + MRV ----------------
    void dfsMRV(int depth) {
        enter(depth);
        if (st.aborted) return;
        int best = -1, bestCnt = 10;
        std::uint16_t bestMask = 0;
        for (int i = 0; i < 81; i++) {
            if (g[i]) continue;
            int r = i / 9, c = i % 9;
            std::uint16_t m = (std::uint16_t)(~(rowM[r] | colM[c] | boxM[boxOf(r, c)]) & 0x1FF);
            int cnt = popcount16(m);
            if (cnt < bestCnt) {
                best = i; bestCnt = cnt; bestMask = m;
                if (cnt <= 1) break;
            }
        }
        if (best == -1) { record(); return; }
        if (bestCnt == 0) return; // poda: célula sem candidatos

        int vals[9], n = 0;
        for (int v = 1; v <= 9; v++)
            if (bestMask & (1u << (v - 1))) vals[n++] = v;
        if (rng) std::shuffle(vals, vals + n, *rng);

        for (int k = 0; k < n; k++) {
            place(best, vals[k]); log(best, vals[k]);
            dfsMRV(depth + 1);
            unplace(best);
            if (stop()) return;
            log(best, 0);
            st.backtracks++;
        }
    }
};

inline long countSolutions(const Grid& p, long limit) {
    Solver s;
    if (!s.load(p)) return 0;
    return s.solveOptimized(limit);
}

inline int countClues(const Grid& p) {
    return (int)std::count_if(p.begin(), p.end(), [](int v) { return v != 0; });
}

inline bool localOk(const Grid& p, int i, int v) {
    int r = i / 9, c = i % 9;
    for (int k = 0; k < 9; k++) {
        if (r * 9 + k != i && p[r * 9 + k] == v) return false;
        if (k * 9 + c != i && p[k * 9 + c] == v) return false;
    }
    int br = (r / 3) * 3, bc = (c / 3) * 3;
    for (int dr = 0; dr < 3; dr++)
        for (int dc = 0; dc < 3; dc++) {
            int j = (br + dr) * 9 + bc + dc;
            if (j != i && p[j] == v) return false;
        }
    return true;
}

inline std::string classify(long c) {
    if (c == 0) return "Impossível (sem solução)";
    if (c == 1) return "Difícil (solução única)";
    if (c <= 10) return "Médio (" + std::to_string(c) + " soluções)";
    return "Fácil (mais de 10 soluções)";
}

inline long peakRSSkb() {
#if defined(__APPLE__)
    rusage ru{}; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss / 1024;
#elif defined(__unix__)
    rusage ru{}; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss;
#else
    return -1;
#endif
}


inline bool parseGrid(const std::string& text, Grid& out) {
    int n = 0;
    for (char ch : text) {
        if (ch >= '1' && ch <= '9') { if (n < 81) out[n] = ch - '0'; n++; }
        else if (ch == '0' || ch == '.') { if (n < 81) out[n] = 0; n++; }
    }
    return n == 81;
}

inline std::string toLine(const Grid& p) {
    std::string s;
    for (int v : p) s += v ? char('0' + v) : '.';
    return s;
}


// Gerador por nível
enum class Level { Easy, Medium, Hard, Impossible };

inline const char* levelName(Level lv) {
    switch (lv) {
        case Level::Easy: return "Fácil";
        case Level::Medium: return "Médio";
        case Level::Hard: return "Difícil";
        default: return "Impossível";
    }
}

inline Grid fullGrid(std::mt19937& rng) {
    Solver s; Grid empty{}; s.load(empty);
    s.setRandom(&rng);
    s.solveOptimized(1);
    return s.solution();
}

inline std::vector<int> shuffledCells(std::mt19937& rng) {
    std::vector<int> v(81); std::iota(v.begin(), v.end(), 0);
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}

// Difícil: remove pistas enquanto a solução continuar única.
inline Grid genHard(std::mt19937& rng) {
    Grid p = fullGrid(rng);
    for (int i : shuffledCells(rng)) {
        int bak = p[i]; p[i] = 0;
        if (countSolutions(p, 2) != 1) p[i] = bak;
    }
    return p;
}

// Médio: remove pistas enquanto houver no máximo 10 soluções; exige >= 2.
inline Grid genMedium(std::mt19937& rng) {
    while (true) {
        Grid p = fullGrid(rng);
        long cnt = 1;
        for (int i : shuffledCells(rng)) {
            int bak = p[i]; p[i] = 0;
            long c = countSolutions(p, 11);
            if (c > 10) p[i] = bak; else cnt = c;
        }
        if (cnt >= 2) return p;
    }
}

// Fácil: remove pistas até passar de 10 soluções.
inline Grid genEasy(std::mt19937& rng) {
    Grid p = fullGrid(rng);
    for (int i : shuffledCells(rng)) {
        p[i] = 0;
        if (countSolutions(p, 11) > 10) break;
    }
    return p;
}

// Impossível: jogo de solução única + um valor que não viola regra visível
// mas difere da solução => nenhuma solução existe.
inline Grid genImpossible(std::mt19937& rng) {
    while (true) {
        Grid p = genHard(rng);
        Solver s; s.load(p); s.solveOptimized(1);
        Grid sol = s.solution();
        for (int i : shuffledCells(rng)) {
            if (p[i]) continue;
            std::array<int, 9> vals{1, 2, 3, 4, 5, 6, 7, 8, 9};
            std::shuffle(vals.begin(), vals.end(), rng);
            for (int v : vals) {
                if (v == sol[i] || !localOk(p, i, v)) continue;
                p[i] = v;
                if (countSolutions(p, 1) == 0) return p;
                p[i] = 0;
            }
        }
    }
}

inline Grid generate(Level lv, std::mt19937& rng) {
    switch (lv) {
        case Level::Easy: return genEasy(rng);
        case Level::Medium: return genMedium(rng);
        case Level::Hard: return genHard(rng);
        default: return genImpossible(rng);
    }
}

} // namespace sudoku
