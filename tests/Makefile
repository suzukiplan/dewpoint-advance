CXX ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

TARGET := highscore_store_test
SOURCES := highscore_store_test.cpp ../src/highscore_store.cpp

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCES) ../src/highscore_store.h ../src/pathutil.h
	$(CXX) $(CXXFLAGS) -I../src $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
