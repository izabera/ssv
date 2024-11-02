CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -ggdb3

all: perf unit
all:

unit: CXXFLAGS += -fsanitize=address,undefined
unit:

perf: CXXFLAGS += -O3 -march=native
perf:

clean:
	rm -f perf unit

.PHONY: all clean
