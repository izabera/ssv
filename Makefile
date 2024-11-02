CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -ggdb3

unit: CXXFLAGS += -fsanitize=address,undefined
unit:

perf: CXXFLAGS += -O3 -march=native
perf:
