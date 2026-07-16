CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

all: makerom

makerom: makerom.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f makerom

.PHONY: all clean
