#include <QtWidgets>

#include <atomic>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>

#include "sudoku_core.hpp"

using namespace sudoku;

static const std::uint64_t NAIVE_LIMIT = 200000000ULL; // trava de segurança do ingênuo
static const std::size_t MAX_TRACE = 400000;           // passos gravados para a animação

// Instâncias embutidas — usadas se puzzles.txt não for encontrado
static const char* BUILTIN_PUZZLES =
    "facil;Wikipedia;530070000600195000098000060800060003400803001700020006060000280000419005000080079\n"
    "facil;Norvig easy01;003020600900305001001806400008102900700000008006708200002609500800203009005010300\n"
    "dificil;AI Escargot;100007090030020008009600500005300900010080002600004000300000010040000007007000300\n"
    "dificil;Inkala 2012;800000000003600000070090200050007000000045700000100030001000068008500010090000400\n"
    "dificil;17 pistas;000000010400000000020000000000050407008000300001090000300400200050100000000806000\n"
    "extremo;Anti-backtracking;..............3.85..1.2.......5.7.....4...1...9.......5......73..2.1........4...9\n";

// ---------------------------------------------------------------------------
// Formatação
// ---------------------------------------------------------------------------
static const QLocale BR(QLocale::Portuguese, QLocale::Brazil); // 1.234,5
static QString fmtD(double v, int prec) { return BR.toString(v, 'f', prec); }
static QString fmtMs(double ms) {
    if (ms < 1) return fmtD(ms, 3) + " ms";
    if (ms < 1000) return fmtD(ms, 1) + " ms";
    return fmtD(ms / 1000.0, 2) + " s";
}
static QString fmtInt(std::uint64_t n) { return BR.toString((qulonglong)n); }
static QString qs(const std::string& s) { return QString::fromStdString(s); }

// ============================================================================
//  BoardWidget — o tabuleiro 9x9 desenhado à mão
// ============================================================================
class BoardWidget : public QWidget {
public:
    enum Kind : std::uint8_t { Empty, Given, User, Solved };

    std::function<void()> onEdited; // chamado quando o usuário altera uma célula

    explicit BoardWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(360, 360);
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
        clearAll();
    }

    QSize sizeHint() const override { return {540, 540}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

    void clearAll() { vals.fill(0); kind.fill(Empty); hot = -1; update(); }

    void setPuzzle(const Grid& p) {
        for (int i = 0; i < 81; i++) { vals[i] = p[i]; kind[i] = p[i] ? Given : Empty; }
        hot = -1; update();
    }
    Grid givens() const {
        Grid g{};
        for (int i = 0; i < 81; i++) if (kind[i] == Given) g[i] = vals[i];
        return g;
    }
    void showSolution(const Grid& s) {
        for (int i = 0; i < 81; i++)
            if (kind[i] != Given) { vals[i] = s[i]; kind[i] = s[i] ? Solved : Empty; }
        hot = -1; update();
    }
    void clearNonGivens() {
        for (int i = 0; i < 81; i++) if (kind[i] != Given) { vals[i] = 0; kind[i] = Empty; }
        hot = -1; update();
    }
    // Usado pela animação (sem update — quem chama decide quando redesenhar)
    void setCell(int i, int v) {
        if (kind[i] == Given) return;
        vals[i] = v; kind[i] = v ? Solved : Empty; hot = i;
    }
    Grid values() const { return vals; }
    // Preenche uma célula com o valor do solver e destaca (usado pela dica)
    void revealCell(int i, int v) {
        if (kind[i] == Given) return;
        vals[i] = v; kind[i] = Solved; hot = i; sel = i; update();
    }
    void setHot(int i) { hot = i; update(); }
    void setEditGivens(bool b) { editGivens = b; update(); }
    void setLocked(bool b) { locked = b; }

    bool conflict(int i) const { return vals[i] && !localOk(vals, i, vals[i]); }
    int conflictCount() const {
        int n = 0;
        for (int i = 0; i < 81; i++) n += conflict(i);
        return n;
    }
    bool isFilled() const {
        return std::all_of(vals.begin(), vals.end(), [](int v) { return v != 0; });
    }
    bool hasUserEntries() const {
        return std::any_of(kind.begin(), kind.end(), [](std::uint8_t k) { return k == User; });
    }

protected:
    QRectF boardRect() const {
        qreal s = qMin(width(), height()) - 4;
        return QRectF((width() - s) / 2.0, (height() - s) / 2.0, s, s);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF R = boardRect();
        qreal c = R.width() / 9.0;
        int selVal = sel >= 0 ? vals[sel] : 0;

        // Fundo das células
        for (int i = 0; i < 81; i++) {
            int r = i / 9, col = i % 9;
            QRectF cell(R.x() + col * c, R.y() + r * c, c, c);
            QColor bg = kind[i] == Given ? QColor("#eceff4") : QColor("#ffffff");
            if (sel >= 0) {
                int sr = sel / 9, sc = sel % 9;
                if (r == sr || col == sc || boxOf(r, col) == boxOf(sr, sc))
                    bg = kind[i] == Given ? QColor("#dfe6f1") : QColor("#eef3fb");
                if (selVal && vals[i] == selVal) bg = QColor("#cddff8");
                if (i == sel) bg = QColor("#aecbf3");
            }
            if (conflict(i)) bg = QColor("#f8d3d3");
            if (i == hot) bg = QColor("#ffe49a");
            p.fillRect(cell, bg);
        }

        // Números
        QFont f = font();
        f.setPixelSize(qMax(10, int(c * 0.56)));
        for (int i = 0; i < 81; i++) {
            if (!vals[i]) continue;
            int r = i / 9, col = i % 9;
            QRectF cell(R.x() + col * c, R.y() + r * c, c, c);
            f.setBold(kind[i] == Given);
            p.setFont(f);
            QColor fg = kind[i] == Given  ? QColor("#1d2330")
                      : kind[i] == User   ? QColor("#2457c5")
                                          : QColor("#0f7b6c");
            if (conflict(i) && kind[i] != Given) fg = QColor("#c0262d");
            p.setPen(fg);
            p.drawText(cell, Qt::AlignCenter, QString::number(vals[i]));
        }

        // Linhas finas e depois as grossas (blocos 3x3)
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k <= 9; k++) {
                bool thick = k % 3 == 0;
                if (thick != (pass == 1)) continue;
                p.setPen(QPen(thick ? QColor("#2b303a") : QColor("#c4c9d2"), thick ? 2.4 : 1.0));
                qreal x = R.x() + k * c, y = R.y() + k * c;
                p.drawLine(QPointF(x, R.top()), QPointF(x, R.bottom()));
                p.drawLine(QPointF(R.left(), y), QPointF(R.right(), y));
            }
        }

        if (editGivens) { // moldura laranja = modo de edição de pistas
            p.setPen(QPen(QColor("#e08a00"), 3));
            p.drawRect(R.adjusted(-1.5, -1.5, 1.5, 1.5));
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QPointF pt = e->position();
#else
        QPointF pt = e->localPos();
#endif
        QRectF R = boardRect();
        if (!R.contains(pt)) { sel = -1; update(); return; }
        qreal c = R.width() / 9.0;
        int col = qBound(0, int((pt.x() - R.x()) / c), 8);
        int r = qBound(0, int((pt.y() - R.y()) / c), 8);
        sel = r * 9 + col;
        setFocus();
        update();
    }

    void keyPressEvent(QKeyEvent* e) override {
        int k = e->key();
        if (sel < 0 && (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down)) {
            sel = 0; update(); return;
        }
        if (sel < 0) { QWidget::keyPressEvent(e); return; }
        int r = sel / 9, c = sel % 9;
        switch (k) {
            case Qt::Key_Left:  sel = r * 9 + (c + 8) % 9; update(); return;
            case Qt::Key_Right: sel = r * 9 + (c + 1) % 9; update(); return;
            case Qt::Key_Up:    sel = ((r + 8) % 9) * 9 + c; update(); return;
            case Qt::Key_Down:  sel = ((r + 1) % 9) * 9 + c; update(); return;
            default: break;
        }
        if (locked) return;
        if (k >= Qt::Key_1 && k <= Qt::Key_9) { edit(sel, k - Qt::Key_0); return; }
        if (k == Qt::Key_0 || k == Qt::Key_Period || k == Qt::Key_Backspace ||
            k == Qt::Key_Delete || k == Qt::Key_Space) { edit(sel, 0); return; }
        QWidget::keyPressEvent(e);
    }

private:
    Grid vals{};
    std::array<std::uint8_t, 81> kind{};
    int sel = -1, hot = -1;
    bool editGivens = false, locked = false;

    void edit(int i, int v) {
        if (editGivens) {
            vals[i] = v; kind[i] = v ? Given : Empty;
        } else {
            if (kind[i] == Given) return;
            vals[i] = v; kind[i] = v ? User : Empty;
        }
        hot = -1;
        update();
        if (onEdited) onEdited();
    }
};

// ============================================================================
//  MainWindow
// ============================================================================
struct BenchItem { QString level, name; Grid p; };

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("Sudoku — Backtracking (Computação de Alto Desempenho)");
        auto* tabs = new QTabWidget;
        tabs->addTab(buildGameTab(), "Jogo");
        tabs->addTab(buildBenchTab(), "Benchmark");
        setCentralWidget(tabs);

        animTimer = new QTimer(this);
        connect(animTimer, &QTimer::timeout, this, [this] { animTick(); });

        // Atalhos
        auto* scPaste = new QShortcut(QKeySequence::Paste, this);
        connect(scPaste, &QShortcut::activated, this, [this] { pastePuzzle(); });
        auto* scCopy = new QShortcut(QKeySequence::Copy, this);
        connect(scCopy, &QShortcut::activated, this, [this] { copyPuzzle(); });
        auto* scGen = new QShortcut(QKeySequence("Ctrl+G"), this);
        connect(scGen, &QShortcut::activated, this, [this] { if (!busy) generatePuzzle(); });
        auto* scSolve = new QShortcut(QKeySequence("F5"), this);
        connect(scSolve, &QShortcut::activated, this, [this] { if (!busy) solve(); });

        // Começa com um jogo difícil gerado
        levelBox->setCurrentIndex(2);
        generatePuzzle();
        resize(1060, 700);
    }

    ~MainWindow() override {
        cancel = true;
        if (worker.joinable()) worker.join();
    }

private:
    // ---- widgets da aba Jogo ----
    BoardWidget* board = nullptr;
    QComboBox *levelBox = nullptr, *algoBox = nullptr;
    QCheckBox* editGivensBox = nullptr;
    QPushButton *btnGen, *btnSolve, *btnCount, *btnAnimate, *btnStop, *btnHint;
    QSlider* speed = nullptr;
    QLabel* speedLabel = nullptr;
    QLabel *lClass, *lClues, *lTime, *lCalls, *lBack, *lDepth, *lSpeedup, *lMem, *lStatus;
    QPlainTextEdit* logBox = nullptr;

    // ---- animação ----
    QTimer* animTimer = nullptr;
    std::vector<Step> trace;
    std::size_t tracePos = 0;
    Grid animFinal{};
    bool animHasSol = false, animTruncated = false, animating = false;
    std::uint64_t animCalls = 0, animBacktracks = 0;

    // ---- aba Benchmark ----
    QTableWidget *benchTable = nullptr, *genTable = nullptr;
    QLabel* benchFileLabel = nullptr;
    QProgressBar* benchProgress = nullptr;
    QPushButton *btnBenchRun, *btnBenchFile, *btnBenchCsv, *btnBenchCancel;
    QString benchPath;

    // ---- thread de trabalho ----
    std::thread worker;
    std::atomic<bool> cancel{false};
    bool busy = false;
    QList<QWidget*> lockable; // desabilitados enquanto a thread trabalha
    std::mt19937 rng{std::random_device{}()};

    // ========================================================================
    //  Construção da interface
    // ========================================================================
    QWidget* buildGameTab() {
        auto* page = new QWidget;
        auto* h = new QHBoxLayout(page);

        // ---- esquerda: tabuleiro + status + log ----
        auto* left = new QVBoxLayout;
        board = new BoardWidget;
        board->onEdited = [this] { onBoardEdited(); };
        left->addWidget(board, 1);
        lStatus = new QLabel;
        lStatus->setWordWrap(true);
        lStatus->setStyleSheet("font-weight:600; padding:4px;");
        left->addWidget(lStatus);
        logBox = new QPlainTextEdit;
        logBox->setReadOnly(true);
        logBox->setMaximumHeight(130);
        logBox->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        left->addWidget(logBox);
        h->addLayout(left, 3);

        // ---- direita: controles ----
        auto* right = new QVBoxLayout;

        auto* gGen = new QGroupBox("Gerar jogo");
        auto* lg = new QHBoxLayout(gGen);
        levelBox = new QComboBox;
        levelBox->addItems({"Fácil  (> 10 soluções)", "Médio  (2 a 10 soluções)",
                            "Difícil  (solução única)", "Impossível  (sem solução)"});
        btnGen = new QPushButton("Gerar");
        btnGen->setToolTip("Ctrl+G");
        lg->addWidget(levelBox, 1);
        lg->addWidget(btnGen);
        right->addWidget(gGen);

        auto* gIn = new QGroupBox("Entrada");
        auto* li = new QGridLayout(gIn);
        editGivensBox = new QCheckBox("Modo edição: o que eu digitar vira pista");
        auto* btnPaste = new QPushButton("Colar texto…");
        auto* btnOpen = new QPushButton("Abrir arquivo…");
        auto* btnCopy = new QPushButton("Copiar");
        auto* btnClearAll = new QPushButton("Tabuleiro vazio");
        auto* btnClearAns = new QPushButton("Apagar respostas");
        btnHint = new QPushButton("Dica");
        btnPaste->setToolTip("Cole 81 caracteres (0 ou . = vazio). Atalho: Ctrl+V");
        btnCopy->setToolTip("Copia o jogo (só as pistas) em uma linha. Atalho: Ctrl+C");
        btnHint->setToolTip("Preenche a célula selecionável mais restrita com o valor correto");
        li->addWidget(editGivensBox, 0, 0, 1, 2);
        li->addWidget(btnPaste, 1, 0);
        li->addWidget(btnOpen, 1, 1);
        li->addWidget(btnCopy, 2, 0);
        li->addWidget(btnHint, 2, 1);
        li->addWidget(btnClearAll, 3, 0);
        li->addWidget(btnClearAns, 3, 1);
        right->addWidget(gIn);

        auto* gSolve = new QGroupBox("Resolver");
        auto* ls = new QGridLayout(gSolve);
        algoBox = new QComboBox;
        algoBox->addItems({"Otimizado (bitmask + MRV)", "Ingênuo (ordem fixa + varredura)",
                           "Comparar os dois"});
        algoBox->setCurrentIndex(2);
        btnSolve = new QPushButton("Resolver");
        btnSolve->setToolTip("F5");
        btnCount = new QPushButton("Contar soluções");
        speed = new QSlider(Qt::Horizontal);
        speed->setRange(0, 120);
        speed->setValue(45);
        speedLabel = new QLabel;
        speedLabel->setMinimumWidth(120);
        btnAnimate = new QPushButton("▶ Animar backtracking");
        btnStop = new QPushButton("■ Parar");
        btnStop->setEnabled(false);
        ls->addWidget(new QLabel("Algoritmo:"), 0, 0);
        ls->addWidget(algoBox, 0, 1);
        ls->addWidget(btnSolve, 1, 0);
        ls->addWidget(btnCount, 1, 1);
        ls->addWidget(new QLabel("Velocidade:"), 2, 0);
        auto* sp = new QHBoxLayout;
        sp->addWidget(speed, 1);
        sp->addWidget(speedLabel);
        ls->addLayout(sp, 2, 1);
        ls->addWidget(btnAnimate, 3, 0);
        ls->addWidget(btnStop, 3, 1);
        right->addWidget(gSolve);

        auto* gRes = new QGroupBox("Resultado");
        auto* lf = new QFormLayout(gRes);
        auto mk = [] { auto* l = new QLabel("—"); l->setTextInteractionFlags(Qt::TextSelectableByMouse); return l; };
        lClass = mk(); lClues = mk(); lTime = mk(); lCalls = mk();
        lBack = mk(); lDepth = mk(); lSpeedup = mk(); lMem = mk();
        lClass->setStyleSheet("font-weight:700;");
        lf->addRow("Classificação:", lClass);
        lf->addRow("Pistas:", lClues);
        lf->addRow("Tempo:", lTime);
        lf->addRow("Chamadas:", lCalls);
        lf->addRow("Retrocessos:", lBack);
        lf->addRow("Prof. máxima:", lDepth);
        lf->addRow("Speedup:", lSpeedup);
        lf->addRow("Memória:", lMem);
        right->addWidget(gRes);
        right->addStretch(1);

        auto* help = new QLabel("Clique numa célula e digite 1–9 · setas movem · 0/Del apaga\n"
                                "Azul = suas respostas · Verde = solver · Vermelho = conflito");
        help->setStyleSheet("color:palette(mid); font-size:11px;");
        right->addWidget(help);

        auto* rightW = new QWidget;
        rightW->setLayout(right);
        rightW->setMinimumWidth(380);
        rightW->setMaximumWidth(460);
        h->addWidget(rightW, 2);

        // ---- conexões ----
        connect(btnGen, &QPushButton::clicked, this, [this] { generatePuzzle(); });
        connect(btnSolve, &QPushButton::clicked, this, [this] { solve(); });
        connect(btnCount, &QPushButton::clicked, this, [this] { countAll(); });
        connect(btnAnimate, &QPushButton::clicked, this, [this] { animate(); });
        connect(btnStop, &QPushButton::clicked, this, [this] {
            if (animating) stopAnimation("Animação interrompida.");
            else if (busy) { cancel = true; setStatus("Cancelando…"); }
        });
        connect(btnPaste, &QPushButton::clicked, this, [this] { pastePuzzle(); });
        connect(btnOpen, &QPushButton::clicked, this, [this] { openPuzzleFile(); });
        connect(btnCopy, &QPushButton::clicked, this, [this] { copyPuzzle(); });
        connect(btnHint, &QPushButton::clicked, this, [this] { giveHint(); });
        connect(btnClearAll, &QPushButton::clicked, this, [this] {
            stopAnimation(); board->clearAll(); editGivensBox->setChecked(true);
            resetStats();
            setStatus("Tabuleiro vazio, modo edição ligado: digite as pistas do seu jogo.");
        });
        connect(btnClearAns, &QPushButton::clicked, this, [this] {
            stopAnimation(); board->clearNonGivens(); setStatus("Respostas apagadas.");
        });
        connect(editGivensBox, &QCheckBox::toggled, this, [this](bool on) {
            board->setEditGivens(on);
            if (!on) updateClassification();
        });
        connect(speed, &QSlider::valueChanged, this, [this] { updateSpeedLabel(); });
        updateSpeedLabel();

        lockable = {btnGen, btnSolve, btnCount, btnAnimate, btnPaste, btnOpen, btnHint,
                    btnClearAll, btnClearAns, levelBox, algoBox, editGivensBox};
        return page;
    }

    QWidget* buildBenchTab() {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);

        auto* top = new QHBoxLayout;
        btnBenchFile = new QPushButton("Escolher arquivo…");
        btnBenchRun = new QPushButton("▶ Rodar benchmark");
        btnBenchCancel = new QPushButton("Cancelar");
        btnBenchCancel->setEnabled(false);
        btnBenchCsv = new QPushButton("Exportar CSV…");
        benchFileLabel = new QLabel;
        top->addWidget(btnBenchFile);
        top->addWidget(benchFileLabel, 1);
        top->addWidget(btnBenchRun);
        top->addWidget(btnBenchCancel);
        top->addWidget(btnBenchCsv);
        v->addLayout(top);

        benchProgress = new QProgressBar;
        benchProgress->setValue(0);
        v->addWidget(benchProgress);

        auto mkTable = [](const QStringList& headers) {
            auto* t = new QTableWidget(0, headers.size());
            t->setHorizontalHeaderLabels(headers);
            t->setEditTriggers(QAbstractItemView::NoEditTriggers);
            t->setSelectionBehavior(QAbstractItemView::SelectRows);
            t->setAlternatingRowColors(true);
            t->verticalHeader()->setVisible(false);
            t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            return t;
        };

        v->addWidget(new QLabel("<b>Instâncias do arquivo</b> — média de 5 execuções "
                                "(duplo clique abre o jogo no tabuleiro)"));
        benchTable = mkTable({"Nível", "Nome", "Pistas", "Soluções", "Otimizado", "Chamadas (opt)",
                              "Ingênuo", "Chamadas (ing)", "Speedup"});
        v->addWidget(benchTable, 3);

        v->addWidget(new QLabel("<b>Jogos gerados</b> — média de 10 por nível (semente fixa 42)"));
        genTable = mkTable({"Nível", "Pistas (média)", "Tempo p/ gerar", "Otimizado",
                            "Chamadas (opt)", "Ingênuo", "Speedup"});
        v->addWidget(genTable, 2);

        auto* note = new QLabel;
        note->setObjectName("benchNote");
        v->addWidget(note);

        // arquivo padrão: ./puzzles.txt ou ao lado do executável; senão, embutido
        for (const QString& cand : {QDir::current().filePath("puzzles.txt"),
                                    QCoreApplication::applicationDirPath() + "/puzzles.txt",
                                    QCoreApplication::applicationDirPath() + "/../puzzles.txt"})
            if (QFileInfo::exists(cand)) { benchPath = QFileInfo(cand).absoluteFilePath(); break; }
        updateBenchLabel();

        connect(btnBenchFile, &QPushButton::clicked, this, [this] {
            QString f = QFileDialog::getOpenFileName(this, "Arquivo de instâncias", benchPath,
                                                     "Texto (*.txt);;Todos (*)");
            if (!f.isEmpty()) { benchPath = f; updateBenchLabel(); }
        });
        connect(btnBenchRun, &QPushButton::clicked, this, [this] { runBenchmark(); });
        connect(btnBenchCancel, &QPushButton::clicked, this, [this] { cancel = true; });
        connect(btnBenchCsv, &QPushButton::clicked, this, [this] { exportCsv(); });
        connect(benchTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            auto* it = benchTable->item(row, 0);
            Grid p{};
            if (!it || !parseGrid(it->data(Qt::UserRole).toString().toStdString(), p)) return;
            loadPuzzle(p, "Instância \"" + benchTable->item(row, 1)->text() + "\" carregada.");
            static_cast<QTabWidget*>(centralWidget())->setCurrentIndex(0);
        });

        lockable << btnBenchFile << btnBenchRun << btnBenchCsv;
        return page;
    }

    // ========================================================================
    //  Infraestrutura
    // ========================================================================
    void post(std::function<void()> f) { QMetaObject::invokeMethod(this, f, Qt::QueuedConnection); }

    bool runAsync(std::function<void()> job) {
        if (busy) return false;
        if (worker.joinable()) worker.join();
        cancel = false;
        setBusy(true);
        worker = std::thread([this, job] {
            job();
            post([this] { setBusy(false); });
        });
        return true;
    }

    void setBusy(bool b) {
        busy = b;
        for (QWidget* w : lockable) w->setEnabled(!b);
        btnStop->setEnabled(b || animating);
        btnBenchCancel->setEnabled(b);
        board->setLocked(b || animating);
        if (b) QApplication::setOverrideCursor(Qt::BusyCursor);
        else QApplication::restoreOverrideCursor();
    }

    void setStatus(const QString& s) { lStatus->setText(s); }
    void log(const QString& s) {
        logBox->appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + s);
    }

    void resetStats() {
        for (QLabel* l : {lClass, lClues, lTime, lCalls, lBack, lDepth, lSpeedup, lMem}) l->setText("—");
    }

    void updateMem() {
        lMem->setText(QString("pico RSS %1 KB (inclui Qt) · solver %2 bytes")
                          .arg(fmtInt(peakRSSkb())).arg(sizeof(Solver)));
    }

    void updateClassification() {
        Grid g = board->givens();
        lClues->setText(QString::number(countClues(g)));
        Solver s;
        if (!s.load(g)) { lClass->setText("Inválido (pistas conflitantes)"); return; }
        s.setCallLimit(2000000); // evita travar a interface em tabuleiros quase vazios
        long n = s.solveOptimized(11);
        lClass->setText(s.stats().aborted ? "— (use Contar soluções)" : qs(classify(n)));
    }

    void loadPuzzle(const Grid& p, const QString& msg) {
        stopAnimation();
        editGivensBox->setChecked(false);
        board->setPuzzle(p);
        resetStats();
        updateClassification();
        setStatus(msg);
        log(msg + "  " + qs(toLine(p)));
    }

    // ========================================================================
    //  Ações da aba Jogo
    // ========================================================================
    void generatePuzzle() {
        stopAnimation();
        Level lv = Level(levelBox->currentIndex());
        QElapsedTimer t; t.start();
        Grid p = generate(lv, rng);
        double ms = t.nsecsElapsed() / 1e6;
        editGivensBox->setChecked(false);
        board->setPuzzle(p);
        resetStats();
        updateClassification();
        QString msg = QString("Jogo %1 gerado em %2 com %3 pistas.")
                          .arg(levelName(lv)).arg(fmtMs(ms)).arg(countClues(p));
        setStatus(msg);
        log(msg);
    }

    void pastePuzzle() {
        if (busy) return;
        Grid p{};
        QString clip = QGuiApplication::clipboard()->text();
        if (parseGrid(clip.toStdString(), p)) { loadPuzzle(p, "Jogo colado da área de transferência."); return; }
        bool ok = false;
        QString txt = QInputDialog::getMultiLineText(
            this, "Colar Sudoku", "81 caracteres (dígitos; 0 ou . para vazio).\n"
                                  "Pode ser uma linha só ou 9 linhas:", "", &ok);
        if (!ok) return;
        if (parseGrid(txt.toStdString(), p)) loadPuzzle(p, "Jogo colado.");
        else QMessageBox::warning(this, "Entrada inválida", "O texto precisa ter exatamente 81 células.");
    }

    void openPuzzleFile() {
        QString f = QFileDialog::getOpenFileName(this, "Abrir Sudoku", QString(), "Texto (*.txt);;Todos (*)");
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString content = QString::fromUtf8(file.readAll());
        Grid p{};
        // aceita um tabuleiro "puro" ou a primeira linha válida no formato do puzzles.txt
        if (!parseGrid(content.toStdString(), p)) {
            bool found = false;
            for (const QString& line : content.split('\n')) {
                if (line.startsWith('#')) continue;
                QString last = line.section(';', -1);
                if (parseGrid(last.toStdString(), p)) { found = true; break; }
            }
            if (!found) { QMessageBox::warning(this, "Arquivo inválido", "Nenhum tabuleiro de 81 células encontrado."); return; }
        }
        loadPuzzle(p, "Arquivo " + QFileInfo(f).fileName() + " carregado.");
    }

    void copyPuzzle() {
        QGuiApplication::clipboard()->setText(qs(toLine(board->givens())));
        setStatus("Jogo copiado (81 caracteres, '.' = vazio).");
    }

    void giveHint() {
        stopAnimation();
        Grid g = board->givens();
        Solver s;
        s.setCallLimit(5000000);
        if (!s.load(g)) { setStatus("Sem dica: as pistas são conflitantes."); return; }
        long n = s.solveOptimized(2);
        if (n == 0) { setStatus("Sem dica: o jogo não tem solução."); return; }
        Grid sol = s.solution();
        // entre as células vazias ou erradas, escolhe a com menos candidatos (a "mais óbvia")
        Grid cur = board->values();
        int best = -1, bestCnt = 10;
        for (int i = 0; i < 81; i++) {
            if (cur[i] == sol[i]) continue;
            Grid t = cur; t[i] = 0;
            int cnt = 0;
            for (int v = 1; v <= 9; v++) cnt += localOk(t, i, v);
            if (cnt < bestCnt) { bestCnt = cnt; best = i; }
        }
        if (best < 0) { setStatus("Tudo certo — não há o que dicar!"); return; }
        board->revealCell(best, sol[best]);
        setStatus(QString("Dica: linha %1, coluna %2 = %3%4")
                      .arg(best / 9 + 1).arg(best % 9 + 1).arg(sol[best])
                      .arg(n > 1 ? "  (o jogo tem várias soluções; esta é uma delas)" : ""));
        if (board->isFilled() && board->conflictCount() == 0) onBoardEdited();
    }

    void onBoardEdited() {
        if (editGivensBox->isChecked()) { lClues->setText(QString::number(countClues(board->givens()))); return; }
        int conf = board->conflictCount();
        if (board->isFilled() && conf == 0) {
            setStatus("🎉 Parabéns! Tabuleiro completo e sem conflitos.");
            log("Jogo resolvido pelo usuário.");
        } else if (conf) {
            setStatus(QString("%1 célula(s) em conflito.").arg(conf));
        } else {
            setStatus("");
        }
    }

    // Executa o(s) solver(es) escolhidos numa thread
    void solve() {
        stopAnimation();
        Grid g = board->givens();
        int algo = algoBox->currentIndex(); // 0 = otimizado, 1 = ingênuo, 2 = comparar
        setStatus("Resolvendo…");
        runAsync([this, g, algo] {
            Solver s;
            s.setCancelFlag(&cancel);
            if (!s.load(g)) {
                post([this] {
                    lClass->setText("Inválido (pistas conflitantes)");
                    setStatus("As pistas violam as regras do Sudoku: jogo inválido.");
                });
                return;
            }
            long count = s.solveOptimized(11);
            bool countAborted = s.stats().aborted;
            Stats so, sn; long fo = 0, fn = 0; Grid sol{};
            if (algo != 1) { fo = s.solveOptimized(1); so = s.stats(); if (fo) sol = s.solution(); }
            if (algo != 0 && !cancel) {
                s.setCallLimit(NAIVE_LIMIT);
                fn = s.solveNaive(1); sn = s.stats();
                if (fn && algo == 1) sol = s.solution();
            }
            bool solved = algo == 1 ? fn > 0 : fo > 0;
            post([=] { showSolveResult(g, algo, count, countAborted, so, fo, sn, fn, sol, solved); });
        });
    }

    void showSolveResult(const Grid& g, int algo, long count, bool countAborted, const Stats& so, long fo,
                         const Stats& sn, long fn, const Grid& sol, bool solved) {
        lClues->setText(QString::number(countClues(g)));
        lClass->setText(countAborted ? "— (interrompido)" : qs(classify(count)));
        auto both = [&](const QString& a, const QString& b) {
            return algo == 0 ? a : algo == 1 ? b : a + "  |  " + b;
        };
        auto naiveTxt = [&](const QString& s) { return sn.aborted ? s + " (interrompido)" : s; };
        lTime->setText(both(fmtMs(so.ms), naiveTxt(fmtMs(sn.ms))));
        lCalls->setText(both(fmtInt(so.calls), fmtInt(sn.calls)));
        lBack->setText(both(fmtInt(so.backtracks), fmtInt(sn.backtracks)));
        lDepth->setText(both(QString::number(so.maxDepth), QString::number(sn.maxDepth)));
        if (algo == 2 && !sn.aborted && so.ms > 0)
            lSpeedup->setText(QString("%1× mais rápido (%2× menos nós)")
                                  .arg(fmtD(sn.ms / so.ms, 1))
                                  .arg(fmtD(double(sn.calls) / qMax<std::uint64_t>(1, so.calls), 1)));
        else lSpeedup->setText(algo == 2 ? "—" : "use \"Comparar os dois\"");
        updateMem();

        if (solved) board->showSolution(sol);
        QString who = algo == 0 ? "Otimizado" : algo == 1 ? "Ingênuo" : "Otimizado | Ingênuo";
        if (cancel) setStatus("Cancelado.");
        else if (solved) setStatus(QString("Resolvido (%1). Classificação: %2").arg(who, lClass->text()));
        else if ((algo == 1 && sn.aborted)) setStatus("O ingênuo atingiu o limite de chamadas.");
        else setStatus("Sem solução: o backtracking esgotou todas as possibilidades.");
        log(QString("Resolver [%1]: %2 | tempo %3 | chamadas %4 | %5")
                .arg(who, lClass->text(), lTime->text(), lCalls->text(), solved ? "resolvido" : "sem solução"));
        Q_UNUSED(fo); Q_UNUSED(fn);
    }

    void countAll() {
        stopAnimation();
        Grid g = board->givens();
        setStatus("Contando soluções (até 100.000)…");
        runAsync([this, g] {
            Solver s;
            s.setCancelFlag(&cancel);
            if (!s.load(g)) { post([this] { setStatus("Pistas conflitantes: 0 soluções."); }); return; }
            const long LIM = 100000;
            long n = s.solveOptimized(LIM);
            Stats st = s.stats();
            post([=] {
                QString cnt = n >= LIM ? "≥ " + fmtInt(LIM) : fmtInt(n);
                if (st.aborted) cnt = "≥ " + fmtInt(n) + " (cancelado)";
                lClass->setText(qs(classify(n)));
                lTime->setText(fmtMs(st.ms));
                lCalls->setText(fmtInt(st.calls));
                lBack->setText(fmtInt(st.backtracks));
                lDepth->setText(QString::number(st.maxDepth));
                updateMem();
                setStatus("Número de soluções: " + cnt);
                log("Contagem de soluções: " + cnt + " em " + fmtMs(st.ms));
            });
        });
    }

    // ========================================================================
    //  Animação do backtracking
    // ========================================================================
    void updateSpeedLabel() {
        int v = speed->value();
        if (v < 20) speedLabel->setText(QString("%1 passos/s").arg(fmtD(1000.0 / animInterval(), 1)));
        else speedLabel->setText(QString("%1 passos/quadro").arg(fmtInt(stepsPerTick())));
        if (animTimer && animTimer->isActive()) animTimer->setInterval(animInterval());
    }
    int stepsPerTick() const {
        int v = speed->value();
        return v < 20 ? 1 : (int)std::lround(std::pow(10.0, (v - 20) / 25.0));
    }
    int animInterval() const {
        int v = speed->value();
        return v < 20 ? 16 + (20 - v) * 25 : 16;
    }

    void animate() {
        stopAnimation();
        Grid g = board->givens();
        bool naive = algoBox->currentIndex() == 1;
        board->clearNonGivens();
        setStatus("Gravando os passos do backtracking…");
        runAsync([this, g, naive] {
            auto tr = std::make_shared<std::vector<Step>>();
            Solver s;
            s.setCancelFlag(&cancel);
            bool ok = s.load(g);
            long f = 0; Grid sol{}; Stats st;
            if (ok) {
                s.setTrace(tr.get(), MAX_TRACE);
                if (naive) { s.setCallLimit(NAIVE_LIMIT); f = s.solveNaive(1); }
                else f = s.solveOptimized(1);
                st = s.stats(); sol = s.solution();
            }
            post([=] {
                if (!ok) { setStatus("Pistas conflitantes: nada para animar."); return; }
                if (cancel) { setStatus("Cancelado."); return; }
                trace = std::move(*tr);
                tracePos = 0; animFinal = sol; animHasSol = f > 0;
                animTruncated = trace.size() >= MAX_TRACE;
                animCalls = st.calls; animBacktracks = st.backtracks;
                animating = true;
                board->setLocked(true);
                btnStop->setEnabled(true);
                animTimer->start(animInterval());
                log(QString("Animação [%1]: %2 passos gravados (%3 chamadas, %4 retrocessos)")
                        .arg(naive ? "ingênuo" : "otimizado").arg(fmtInt(trace.size()))
                        .arg(fmtInt(st.calls)).arg(fmtInt(st.backtracks)));
            });
        });
    }

    void animTick() {
        int n = stepsPerTick();
        std::uint64_t undos = 0;
        for (int k = 0; k < n && tracePos < trace.size(); k++, tracePos++) {
            const Step& s = trace[tracePos];
            board->setCell(s.first, s.second);
            if (s.second == 0) undos++;
        }
        board->update();
        setStatus(QString("Animando: passo %1 de %2%3")
                      .arg(fmtInt(tracePos)).arg(fmtInt(trace.size()))
                      .arg(animTruncated ? " (limite de gravação)" : ""));
        if (tracePos >= trace.size()) finishAnimation();
    }

    void finishAnimation() {
        animTimer->stop();
        animating = false;
        board->setLocked(busy);
        btnStop->setEnabled(busy);
        if (animHasSol) board->showSolution(animFinal);
        board->setHot(-1);
        if (!animHasSol)
            setStatus(QString("Sem solução: o backtracking testou e desfez tudo (%1 chamadas).").arg(fmtInt(animCalls)));
        else if (animTruncated)
            setStatus(QString("A busca teve %1 chamadas; a animação mostrou os primeiros %2 passos. Solução final exibida.")
                          .arg(fmtInt(animCalls)).arg(fmtInt(trace.size())));
        else
            setStatus(QString("Solução encontrada: %1 chamadas, %2 retrocessos.")
                          .arg(fmtInt(animCalls)).arg(fmtInt(animBacktracks)));
        trace.clear(); trace.shrink_to_fit();
    }

    void stopAnimation(const QString& msg = QString()) {
        if (!animating) return;
        animTimer->stop();
        animating = false;
        trace.clear(); trace.shrink_to_fit();
        board->clearNonGivens();
        board->setLocked(busy);
        btnStop->setEnabled(busy);
        if (!msg.isEmpty()) setStatus(msg);
    }

    // ========================================================================
    //  Benchmark
    // ========================================================================
    void updateBenchLabel() {
        benchFileLabel->setText(benchPath.isEmpty() ? "Instâncias: lista embutida (puzzles.txt não encontrado)"
                                                    : "Instâncias: " + benchPath);
    }

    std::vector<BenchItem> loadBenchItems() {
        QString content;
        if (!benchPath.isEmpty()) {
            QFile f(benchPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) content = QString::fromUtf8(f.readAll());
        }
        if (content.isEmpty()) content = BUILTIN_PUZZLES;
        std::vector<BenchItem> items;
        for (const QString& raw : content.split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            QStringList parts = line.split(';');
            BenchItem it;
            if (parts.size() >= 3) { it.level = parts[0]; it.name = parts[1]; }
            else { it.level = "?"; it.name = QString("linha %1").arg(items.size() + 1); }
            if (parseGrid(parts.last().toStdString(), it.p)) items.push_back(it);
        }
        return items;
    }

    static QTableWidgetItem* cell(const QString& s, bool right = false) {
        auto* it = new QTableWidgetItem(s);
        if (right) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return it;
    }

    void runBenchmark() {
        stopAnimation();
        auto items = loadBenchItems();
        benchTable->setRowCount(0);
        genTable->setRowCount(0);
        benchProgress->setRange(0, int(items.size()) + 4);
        benchProgress->setValue(0);
        runAsync([this, items] {
            const int REPS = 5;
            int done = 0;
            for (const BenchItem& b : items) {
                if (cancel) break;
                Solver s;
                s.setCancelFlag(&cancel);
                if (!s.load(b.p)) {
                    int d = ++done;
                    post([=] { addBenchRow(b, -1, {}, {}, 0, 0); benchProgress->setValue(d); });
                    continue;
                }
                long sols = s.solveOptimized(11);
                Stats opt; double optMs = 0;
                for (int k = 0; k < REPS; k++) { s.solveOptimized(1); opt = s.stats(); optMs += opt.ms; }
                optMs /= REPS;
                s.setCallLimit(NAIVE_LIMIT);
                Stats nv; double nvMs = 0; int nreps = 0;
                for (int k = 0; k < REPS && !cancel; k++) {
                    s.solveNaive(1); nv = s.stats(); nvMs += nv.ms; nreps++;
                    if (nv.ms > 2000) break; // casos lentos: uma execução basta
                }
                nvMs /= qMax(1, nreps);
                int d = ++done;
                post([=] { addBenchRow(b, sols, opt, nv, optMs, nvMs); benchProgress->setValue(d); });
            }

            std::mt19937 grng(42);
            const int N = 10;
            for (Level lv : {Level::Easy, Level::Medium, Level::Hard, Level::Impossible}) {
                if (cancel) break;
                double gen = 0, om = 0, nm = 0, clues = 0, oc = 0;
                for (int k = 0; k < N && !cancel; k++) {
                    auto t0 = std::chrono::steady_clock::now();
                    Grid p = generate(lv, grng);
                    gen += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    clues += countClues(p);
                    Solver s; s.setCancelFlag(&cancel); s.load(p);
                    s.solveOptimized(1); om += s.stats().ms; oc += s.stats().calls;
                    s.setCallLimit(NAIVE_LIMIT);
                    s.solveNaive(1); nm += s.stats().ms;
                }
                int d = ++done;
                post([=] {
                    int r = genTable->rowCount();
                    genTable->insertRow(r);
                    genTable->setItem(r, 0, cell(levelName(lv)));
                    genTable->setItem(r, 1, cell(fmtD(clues / N, 1), true));
                    genTable->setItem(r, 2, cell(fmtMs(gen / N), true));
                    genTable->setItem(r, 3, cell(fmtMs(om / N), true));
                    genTable->setItem(r, 4, cell(fmtInt((std::uint64_t)(oc / N)), true));
                    genTable->setItem(r, 5, cell(fmtMs(nm / N), true));
                    genTable->setItem(r, 6, cell(QString("%1×").arg(fmtD(om > 0 ? nm / om : 0, 1)), true));
                    benchProgress->setValue(d);
                });
            }
            post([this] {
                if (auto* note = findChild<QLabel*>("benchNote"))
                    note->setText(QString("Pico de memória do processo: %1 KB (inclui a interface Qt). "
                                          "Estado do solver: %2 bytes.%3")
                                      .arg(fmtInt(peakRSSkb())).arg(sizeof(Solver))
                                      .arg(cancel ? "  — benchmark cancelado." : ""));
            });
        });
    }

    void addBenchRow(const BenchItem& b, long sols, const Stats& opt, const Stats& nv, double optMs, double nvMs) {
        int r = benchTable->rowCount();
        benchTable->insertRow(r);
        auto* first = cell(b.level);
        first->setData(Qt::UserRole, qs(toLine(b.p)));
        benchTable->setItem(r, 0, first);
        benchTable->setItem(r, 1, cell(b.name));
        benchTable->setItem(r, 2, cell(QString::number(countClues(b.p)), true));
        if (sols < 0) {
            benchTable->setItem(r, 3, cell("pistas conflitantes"));
            return;
        }
        benchTable->setItem(r, 3, cell(sols > 10 ? ">10" : QString::number(sols), true));
        benchTable->setItem(r, 4, cell(fmtMs(optMs), true));
        benchTable->setItem(r, 5, cell(fmtInt(opt.calls), true));
        benchTable->setItem(r, 6, cell(fmtMs(nvMs) + (nv.aborted ? " (interr.)" : ""), true));
        benchTable->setItem(r, 7, cell(fmtInt(nv.calls), true));
        benchTable->setItem(r, 8, cell(nv.aborted || optMs <= 0 ? "—"
                                       : QString("%1×").arg(fmtD(nvMs / optMs, 1)), true));
    }

    void exportCsv() {
        if (benchTable->rowCount() == 0 && genTable->rowCount() == 0) {
            QMessageBox::information(this, "Exportar CSV", "Rode o benchmark primeiro.");
            return;
        }
        QString f = QFileDialog::getSaveFileName(this, "Salvar CSV", "resultados.csv", "CSV (*.csv)");
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);
        auto dump = [&](QTableWidget* t) {
            QStringList h;
            for (int c = 0; c < t->columnCount(); c++) h << t->horizontalHeaderItem(c)->text();
            out << h.join(';') << "\n";
            for (int r = 0; r < t->rowCount(); r++) {
                QStringList row;
                for (int c = 0; c < t->columnCount(); c++) row << (t->item(r, c) ? t->item(r, c)->text() : "");
                out << row.join(';') << "\n";
            }
        };
        dump(benchTable);
        out << "\n";
        dump(genTable);
        setStatus("CSV salvo em " + f);
    }
};

// ============================================================================
int main(int argc, char** argv) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName("Sudoku Backtracking");
    MainWindow w;
    w.show();
    return app.exec();
}
