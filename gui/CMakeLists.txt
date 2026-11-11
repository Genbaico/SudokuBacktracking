cmake_minimum_required(VERSION 3.16)
project(SudokuGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Usa Qt6 se existir, senão Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(sudoku_gui sudoku_gui.cpp sudoku_core.hpp)
target_link_libraries(sudoku_gui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Threads::Threads)

# Copia puzzles.txt para a pasta do executável (a aba Benchmark procura lá)
if(EXISTS ${CMAKE_SOURCE_DIR}/puzzles.txt)
  configure_file(${CMAKE_SOURCE_DIR}/puzzles.txt ${CMAKE_BINARY_DIR}/puzzles.txt COPYONLY)
endif()
