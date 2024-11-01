CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -ggdb3
ifndef DEBUG
	CXXFLAGS += -O3 -march=native
else
	CXXFLAGS += -fsanitize=address,undefined
endif
ssv:
