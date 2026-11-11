#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace std;

using Grid = array<int, 81>;

static inline int boxOf(int r, int c) { return (r / 3) * 3 + c / 3; }

static inline int popcount16(uint16_t m) {
    int c = 0;
    while (m) { m &= (uint16_t)(m - 1); c++; }
    return c;
}


struct Stats {
    uint64_t calls = 0;      
    uint64_t backtracks = 0;  
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

    void setRandom(mt19937* r) { rng = r; }          
    void setCallLimit(uint64_t l) { callLimit = l; } 

    long solveOptimized(long lim = 1) { return run(lim, true); }
    long solveNaive(long lim = 1) { return run(lim, false); }

    const Grid& solution() const { return firstSol; }
    const Stats& stats() const { return st; }

private:
    Grid g{}, firstSol{};
    array<uint16_t, 9> rowM{}, colM{}, boxM{};
    Stats st;
    long limit = 1, found = 0;
    uint64_t callLimit = 0;
    mt19937* rng = nullptr;

    bool canPlace(int i, int v) const {
        uint16_t b = (uint16_t)(1u << (v - 1));
        int r = i / 9, c = i % 9;
        return !((rowM[r] | colM[c] | boxM[boxOf(r, c)]) & b);
    }
    void place(int i, int v) {
        uint16_t b = (uint16_t)(1u << (v - 1));
        int r = i / 9, c = i % 9;
        g[i] = v; rowM[r] |= b; colM[c] |= b; boxM[boxOf(r, c)] |= b;
    }
    void unplace(int i) {
        uint16_t b = (uint16_t)~(1u << (g[i] - 1));
        int r = i / 9, c = i % 9;
        rowM[r] &= b; colM[c] &= b; boxM[boxOf(r, c)] &= b; g[i] = 0;
    }

    long run(long lim, bool optimized) {
        limit = lim; found = 0; st = Stats{};
        Grid gb = g; auto rb = rowM, cb = colM, bb = boxM;
        auto t0 = chrono::steady_clock::now();
        if (optimized) dfsMRV(0); else dfsNaive(0, 0);
        auto t1 = chrono::steady_clock::now();
        st.ms = chrono::duration<double, milli>(t1 - t0).count();
        g = gb; rowM = rb; colM = cb; boxM = bb; 
        return found;
    }

    bool stop() const { return found >= limit || st.aborted; }

    void enter(int depth) {
        st.calls++;
        if (depth > st.maxDepth) st.maxDepth = depth;
        if (callLimit && st.calls >= callLimit) st.aborted = true;
    }

    void record() {
        if (found == 0) firstSol = g;
        found++;
    }

    // ---------------- Solver 1: backtracking ingênuo ----------------
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
        while (pos < 81 && g[pos]) pos++;   // próxima célula vazia, em ordem
        if (pos == 81) { record(); return; }
        for (int v = 1; v <= 9; v++) {
            if (!isSafeScan(pos, v)) continue;
            g[pos] = v;                      // escolhe
            dfsNaive(pos + 1, depth + 1);    // explora
            g[pos] = 0;                      // desfaz (backtrack)
            if (stop()) return;
            st.backtracks++;
        }
    }

    // ---------------- Solver 2: bitmask + MRV ----------------
    void dfsMRV(int depth) {
        enter(depth);
        if (st.aborted) return;

        // MRV: escolhe a célula vazia com o menor número de candidatos
        int best = -1, bestCnt = 10;
        uint16_t bestMask = 0;
        for (int i = 0; i < 81; i++) {
            if (g[i]) continue;
            int r = i / 9, c = i % 9;
            uint16_t m = (uint16_t)(~(rowM[r] | colM[c] | boxM[boxOf(r, c)]) & 0x1FF);
            int cnt = popcount16(m);
            if (cnt < bestCnt) {
                best = i; bestCnt = cnt; bestMask = m;
                if (cnt <= 1) break; // não há escolha melhor que 0 ou 1
            }
        }
        if (best == -1) { record(); return; } // tabuleiro completo
        if (bestCnt == 0) return;             // beco sem saída: poda imediata

        int vals[9], n = 0;
        for (int v = 1; v <= 9; v++)
            if (bestMask & (1u << (v - 1))) vals[n++] = v;
        if (rng) shuffle(vals, vals + n, *rng);

        for (int k = 0; k < n; k++) {
            place(best, vals[k]);
            dfsMRV(depth + 1);
            unplace(best);
            if (stop()) return;
            st.backtracks++;
        }
    }
};

long countSolutions(const Grid& p, long limit) {
    Solver s;
    if (!s.load(p)) return 0;
    return s.solveOptimized(limit);
}

int countClues(const Grid& p) {
    return (int)count_if(p.begin(), p.end(), [](int v) { return v != 0; });
}

bool localOk(const Grid& p, int i, int v) {
    int r = i / 9, c = i % 9;
    for (int k = 0; k < 9; k++)
        if (p[r * 9 + k] == v || p[k * 9 + c] == v) return false;
    int br = (r / 3) * 3, bc = (c / 3) * 3;
    for (int dr = 0; dr < 3; dr++)
        for (int dc = 0; dc < 3; dc++)
            if (p[(br + dr) * 9 + bc + dc] == v) return false;
    return true;
}

string classify(long c) {
    if (c == 0) return "Impossivel (sem solucao)";
    if (c == 1) return "Dificil (solucao unica)";
    if (c <= 10) return "Medio (" + to_string(c) + " solucoes)";
    return "Facil (mais de 10 solucoes)";
}

long peakRSSkb() {
#if defined(__APPLE__)
    rusage ru{}; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss / 1024;
#elif defined(__unix__)
    rusage ru{}; getrusage(RUSAGE_SELF, &ru); return ru.ru_maxrss;
#else
    return -1;
#endif
}

bool parseGrid(const string& text, Grid& out) {
    int n = 0;
    for (char ch : text) {
        if (ch >= '1' && ch <= '9') { if (n < 81) out[n] = ch - '0'; n++; }
        else if (ch == '0' || ch == '.') { if (n < 81) out[n] = 0; n++; }
    }
    return n == 81;
}

void printGrid(const Grid& p, const string& title = "") {
    if (!title.empty()) cout << "\n" << title << "\n";
    for (int r = 0; r < 9; r++) {
        if (r % 3 == 0) cout << "+-------+-------+-------+\n";
        for (int c = 0; c < 9; c++) {
            if (c % 3 == 0) cout << "| ";
            int v = p[r * 9 + c];
            cout << (v ? char('0' + v) : '.') << ' ';
        }
        cout << "|\n";
    }
    cout << "+-------+-------+-------+\n";
}

void printStats(const string& name, const Stats& s, long found) {
    cout << "  [" << name << "]\n"
         << "    tempo ............ " << fixed << setprecision(3) << s.ms << " ms\n"
         << "    chamadas ......... " << s.calls << "\n"
         << "    retrocessos ...... " << s.backtracks << "\n"
         << "    prof. maxima ..... " << s.maxDepth << "\n"
         << "    resultado ........ "
         << (s.aborted ? "INTERROMPIDO (limite de chamadas)" : (found ? "resolvido" : "sem solucao"))
         << "\n";
}


// Gerador por nível de dificuldade
enum class Level { Easy, Medium, Hard, Impossible };

Grid fullGrid(mt19937& rng) {
    Solver s; Grid empty{}; s.load(empty);
    s.setRandom(&rng);              
    s.solveOptimized(1);
    return s.solution();
}

vector<int> shuffledCells(mt19937& rng) {
    vector<int> v(81); iota(v.begin(), v.end(), 0);
    shuffle(v.begin(), v.end(), rng);
    return v;
}

// Difícil: remove pistas enquanto a solução continuar ÚNICA.
Grid genHard(mt19937& rng) {
    Grid p = fullGrid(rng);
    for (int i : shuffledCells(rng)) {
        int bak = p[i]; p[i] = 0;
        if (countSolutions(p, 2) != 1) p[i] = bak;
    }
    return p;
}

// Médio: remove pistas enquanto houver no máximo 10 soluções; exige >= 2 no fim.
Grid genMedium(mt19937& rng) {
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

// Fácil: remove pistas até o jogo passar a ter mais de 10 soluções.
Grid genEasy(mt19937& rng) {
    Grid p = fullGrid(rng);
    for (int i : shuffledCells(rng)) {
        p[i] = 0;
        if (countSolutions(p, 11) > 10) break;
    }
    return p;
}

// Impossível: parte de um jogo de solução única e insere, numa célula vazia,
// um valor que NÃO viola nenhuma regra visível mas difere da única solução.
// Resultado: nenhuma pista conflita, mas o jogo não tem solução.
Grid genImpossible(mt19937& rng) {
    while (true) {
        Grid p = genHard(rng);
        Solver s; s.load(p); s.solveOptimized(1);
        Grid sol = s.solution();
        for (int i : shuffledCells(rng)) {
            if (p[i]) continue;
            array<int, 9> vals{1, 2, 3, 4, 5, 6, 7, 8, 9};
            shuffle(vals.begin(), vals.end(), rng);
            for (int v : vals) {
                if (v == sol[i] || !localOk(p, i, v)) continue;
                p[i] = v;
                if (countSolutions(p, 1) == 0) return p;
                p[i] = 0;
            }
        }
    }
}

Grid generate(Level lv, mt19937& rng) {
    switch (lv) {
        case Level::Easy: return genEasy(rng);
        case Level::Medium: return genMedium(rng);
        case Level::Hard: return genHard(rng);
        default: return genImpossible(rng);
    }
}

const char* levelName(Level lv) {
    switch (lv) {
        case Level::Easy: return "Facil";
        case Level::Medium: return "Medio";
        case Level::Hard: return "Dificil";
        default: return "Impossivel";
    }
}

const uint64_t NAIVE_LIMIT = 200000000ULL; 

void analyze(const Grid& p) {
    printGrid(p, "Tabuleiro (" + to_string(countClues(p)) + " pistas):");
    Solver s;
    if (!s.load(p)) {
        cout << "\nAs pistas iniciais violam as regras -> jogo invalido / impossivel.\n";
        return;
    }

    long n = s.solveOptimized(11);
    cout << "\nClassificacao: " << classify(n) << "\n";
    printStats("Contagem de solucoes (ate 11)", s.stats(), n);

    long f = s.solveOptimized(1);
    Stats opt = s.stats();
    if (f) printGrid(s.solution(), "Solucao:");
    cout << "\nDesempenho:\n";
    printStats("Otimizado: bitmask + MRV", opt, f);

    s.setCallLimit(NAIVE_LIMIT);
    long fn = s.solveNaive(1);
    printStats("Ingenuo: ordem fixa + varredura", s.stats(), fn);

    if (!s.stats().aborted && opt.ms > 0)
        cout << "  Speedup do otimizado: " << fixed << setprecision(1)
             << s.stats().ms / opt.ms << "x\n";
    cout << "  Memoria: pico do processo (RSS) = " << peakRSSkb() << " KB; "
         << "estado do solver = " << sizeof(Solver) << " bytes\n";
}

void benchmark(const string& file, int reps = 5) {
    ifstream in(file);
    if (!in) { cout << "Nao foi possivel abrir " << file << "\n"; return; }
    ofstream csv("resultados.csv");
    csv << "nivel,nome,pistas,solucoes,opt_ms,opt_chamadas,opt_retrocessos,"
           "naive_ms,naive_chamadas,naive_retrocessos,naive_interrompido\n";

    cout << "\n=== Benchmark de instancias (" << file << ") — media de " << reps
         << " execucoes ===\n\n";
    cout << left << setw(11) << "nivel" << setw(18) << "nome" << right << setw(7) << "pistas"
         << setw(6) << "sol" << setw(12) << "opt ms" << setw(12) << "opt cham."
         << setw(14) << "naive ms" << setw(14) << "naive cham." << "\n";

    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        stringstream ss(line);
        string lvl, name, puz;
        getline(ss, lvl, ';'); getline(ss, name, ';'); getline(ss, puz);
        Grid p{};
        if (!parseGrid(puz, p)) { cout << "linha ignorada: " << name << "\n"; continue; }

        Solver s;
        if (!s.load(p)) { cout << name << ": pistas conflitantes\n"; continue; }
        long sols = s.solveOptimized(11);

        double optMs = 0; Stats opt;
        for (int k = 0; k < reps; k++) { s.solveOptimized(1); opt = s.stats(); optMs += opt.ms; }
        optMs /= reps;

        s.setCallLimit(NAIVE_LIMIT);
        double nvMs = 0; Stats nv;
        int nreps = reps;
        for (int k = 0; k < nreps; k++) {
            s.solveNaive(1); nv = s.stats(); nvMs += nv.ms;
            if (nv.ms > 2000) { nreps = k + 1; break; } 
        }
        nvMs /= nreps;

        cout << left << setw(11) << lvl << setw(18) << name.substr(0, 17) << right
             << setw(7) << countClues(p) << setw(6) << (sols > 10 ? string(">10") : to_string(sols))
             << setw(12) << fixed << setprecision(3) << optMs << setw(12) << opt.calls
             << setw(14) << nvMs << setw(14) << nv.calls << (nv.aborted ? " (interrompido)" : "")
             << "\n";
        csv << lvl << ',' << name << ',' << countClues(p) << ',' << sols << ',' << optMs << ','
            << opt.calls << ',' << opt.backtracks << ',' << nvMs << ',' << nv.calls << ','
            << nv.backtracks << ',' << (nv.aborted ? 1 : 0) << '\n';
    }


    const int N = 10;
    cout << "\n=== Jogos gerados (" << N << " por nivel, medias) ===\n\n";
    cout << left << setw(12) << "nivel" << right << setw(10) << "pistas" << setw(14) << "gerar ms"
         << setw(12) << "opt ms" << setw(12) << "opt cham." << setw(12) << "naive ms" << "\n";
    mt19937 rng(42); // semente fixa = resultados reprodutíveis
    for (Level lv : {Level::Easy, Level::Medium, Level::Hard, Level::Impossible}) {
        double gen = 0, om = 0, nm = 0, clues = 0, oc = 0;
        for (int k = 0; k < N; k++) {
            auto t0 = chrono::steady_clock::now();
            Grid p = generate(lv, rng);
            gen += chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            clues += countClues(p);
            Solver s; s.load(p);
            s.solveOptimized(1); om += s.stats().ms; oc += s.stats().calls;
            s.setCallLimit(NAIVE_LIMIT);
            s.solveNaive(1); nm += s.stats().ms;
        }
        cout << left << setw(12) << levelName(lv) << right << fixed << setprecision(1)
             << setw(10) << clues / N << setw(14) << setprecision(2) << gen / N
             << setw(12) << setprecision(3) << om / N << setw(12) << setprecision(0) << oc / N
             << setw(12) << setprecision(3) << nm / N << "\n";
    }
    cout << "\nPico de memoria do processo (RSS): " << peakRSSkb() << " KB\n";
    cout << "Resultados das instancias salvos em resultados.csv\n";
}


// Interface de console
int readInt(const string& prompt) {
    cout << prompt;
    string s; getline(cin, s);
    try { return stoi(s); } catch (...) { return -1; }
}

int main(int argc, char** argv) {
    if (argc >= 3 && string(argv[1]) == "bench") { benchmark(argv[2]); return 0; }

    mt19937 rng(random_device{}());
    while (true) {
        cout << "\n========== SUDOKU - BACKTRACKING ==========\n"
                "1. Digitar um Sudoku (9 linhas)\n"
                "2. Colar um Sudoku em uma linha (81 caracteres)\n"
                "3. Gerar Sudoku por nivel de dificuldade\n"
                "4. Carregar Sudoku de arquivo\n"
                "5. Benchmark (puzzles.txt)\n"
                "0. Sair\n";
        int op = readInt("Opcao: ");
        if (op == 0) break;

        Grid p{};
        if (op == 1) {
            cout << "Digite 9 linhas com 9 digitos cada (use 0 ou . para vazio):\n";
            string all, l;
            for (int r = 0; r < 9 && getline(cin, l); r++) all += l;
            if (parseGrid(all, p)) analyze(p); else cout << "Entrada invalida (precisa de 81 celulas).\n";
        } else if (op == 2) {
            cout << "Cole os 81 caracteres: ";
            string l; getline(cin, l);
            if (parseGrid(l, p)) analyze(p); else cout << "Entrada invalida (precisa de 81 celulas).\n";
        } else if (op == 3) {
            int lv = readInt("Nivel: 1-Facil  2-Medio  3-Dificil  4-Impossivel: ");
            if (lv < 1 || lv > 4) { cout << "Nivel invalido.\n"; continue; }
            Level L = Level(lv - 1);
            auto t0 = chrono::steady_clock::now();
            p = generate(L, rng);
            double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            cout << "Jogo " << levelName(L) << " gerado em " << fixed << setprecision(2) << ms << " ms\n";
            analyze(p);
        } else if (op == 4) {
            cout << "Caminho do arquivo: ";
            string path; getline(cin, path);
            ifstream f(path);
            stringstream buf; buf << f.rdbuf();
            if (f && parseGrid(buf.str(), p)) analyze(p); else cout << "Arquivo invalido.\n";
        } else if (op == 5) {
            benchmark("puzzles.txt");
        } else {
            cout << "Opcao invalida.\n";
        }
    }
    return 0;
}
