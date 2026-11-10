# Sudoku — Interface Gráfica (Qt)

Versão com janela do solver de Sudoku. Usa a mesma lógica da versão de console (`sudoku_core.hpp`).

## Arquivos

- `sudoku_gui.cpp` — interface (janela, tabuleiro, botões, benchmark)
- `sudoku_core.hpp` — solver (ingênuo e otimizado) + gerador por nível
- `CMakeLists.txt` — build (detecta Qt6 ou Qt5 sozinho)
- `puzzles.txt` — instâncias para a aba Benchmark

## Instalar o Qt (só uma vez)

- Debian / Ubuntu: `sudo apt install build-essential cmake qtbase5-dev`
- CachyOS / Arch: `sudo pacman -S base-devel cmake qt6-base`

## Compilar e rodar

Com CMake (recomendado):

```bash
cmake -B build
cmake --build build
./build/sudoku_gui
```

Ou direto com g++:

```bash
g++ -O2 -std=c++17 -fPIC sudoku_gui.cpp -o sudoku_gui $(pkg-config --cflags --libs Qt5Widgets) -pthread
./sudoku_gui
```

(No Qt6 troque `Qt5Widgets` por `Qt6Widgets`.)

## Como usar

**Aba Jogo**
- Clique numa célula e digite 1–9 as setas movem e 0, Del ou Backspace apagam.
- Cores: pistas em negrito, suas respostas em azul, as do solver em verde, conflitos em vermelho.
- *Gerar*: cria um jogo do nível escolhido (Fácil, Médio, Difícil, Impossível), tendo o atalho Ctrl+G.
- *Modo edição*: o que você digitar vira pista, poder ser usado para digitar um jogo do sudoku.com (ou *Tabuleiro vazio*, que já liga o modo).
- *Colar texto* (Ctrl+V): aceita 81 caracteres, em uma linha ou em 9.
- *Resolver* (F5): otimizado, ingênuo ou os dois, com tempo, chamadas, retrocessos, profundidade e speedup.
- *Contar soluções*: conta até 100.000 soluções.
- *Animar backtracking*: mostra o algoritmo preenchendo e desfazendo células (a célula amarela é a atual), o controle deslizante ajusta a velocidade e usando o ingênuo, dá para ver na tela por que ele é tão mais lento.
- *Dica*: preenche a célula mais restrita com o valor correto.

**Aba Benchmark**
- Roda as instâncias de `puzzles.txt` (média de 5 execuções) e 10 jogos gerados por nível.
- Duplo clique numa linha abre aquele jogo no tabuleiro.
- *Exportar CSV* salva as duas tabelas.

As buscas rodam numa thread separada, então a janela não trava, e o botão *Parar*/*Cancelar* interrompe a qualquer momento.
