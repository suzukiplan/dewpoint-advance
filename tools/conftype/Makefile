CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

all: conftype

conftype: conftype.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test: conftype
	./test.sh

clean:
	rm -f conftype

.PHONY: all clean test
