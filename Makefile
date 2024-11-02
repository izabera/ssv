CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -ggdb3
#ifndef DEBUG
#	CXXFLAGS += -O3 -march=native
#else
#	CXXFLAGS += -fsanitize=address,undefined
#endif

debug: CXXFLAGS += -fsanitize=address,undefined
debug: ssv

perf: CXXFLAGS += -O3 -march=native
perf: ssv

.PHONY: debug perf
