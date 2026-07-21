CXX ?= c++
CC ?= cc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic

TARGETS := highscore_store_test ugc_codec_test
CODEC_BUILD_DIR := build
CODEC_OBJECTS := $(CODEC_BUILD_DIR)/ugc_codec.o \
	$(CODEC_BUILD_DIR)/lz4.o \
	$(CODEC_BUILD_DIR)/lz4frame.o \
	$(CODEC_BUILD_DIR)/lz4hc.o \
	$(CODEC_BUILD_DIR)/xxhash.o

.PHONY: all test clean

all: $(TARGETS)

highscore_store_test: highscore_store_test.cpp ../src/highscore_store.cpp ../src/highscore_store.h ../src/pathutil.h
	$(CXX) $(CXXFLAGS) -I../src highscore_store_test.cpp ../src/highscore_store.cpp -o $@

ugc_codec_test: ugc_codec_test.cpp $(CODEC_OBJECTS)
	$(CXX) $(CXXFLAGS) -I../src ugc_codec_test.cpp $(CODEC_OBJECTS) -o $@

$(CODEC_BUILD_DIR)/ugc_codec.o: ../src/ugc_codec.cpp ../src/ugc_codec.h ../src/lz4/lz4frame.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../src -c $< -o $@

$(CODEC_BUILD_DIR)/%.o: ../src/lz4/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../src/lz4 -c $< -o $@

test: $(TARGETS)
	./highscore_store_test
	./ugc_codec_test

clean:
	rm -f $(TARGETS)
	rm -rf $(CODEC_BUILD_DIR)
