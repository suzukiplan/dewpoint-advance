CXX ?= c++
CXXFLAGS ?= -O2 -Wall -Wextra -pedantic
MAKEROM_CXXFLAGS := -std=c++11

all: makerom

makerom: makerom.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(MAKEROM_CXXFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f makerom

.PHONY: all clean
