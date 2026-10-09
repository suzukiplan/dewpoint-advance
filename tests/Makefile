CXX ?= c++
CC ?= cc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic

TARGETS := runtime_config_test highscore_store_test keymap_test log_timestamp_test steam_utils_compat_test ugc_codec_test
CODEC_BUILD_DIR := build
CODEC_OBJECTS := $(CODEC_BUILD_DIR)/ugc_codec.o \
	$(CODEC_BUILD_DIR)/lz4.o \
	$(CODEC_BUILD_DIR)/lz4frame.o \
	$(CODEC_BUILD_DIR)/lz4hc.o \
	$(CODEC_BUILD_DIR)/xxhash.o

.PHONY: all test clean

all: $(TARGETS)

runtime_config_test: runtime_config_test.cpp ../src/runtime_config.h
	$(CXX) $(CXXFLAGS) -I../src runtime_config_test.cpp -o $@

highscore_store_test: highscore_store_test.cpp ../src/highscore_store.cpp ../src/highscore_store.h ../src/pathutil.h
	$(CXX) $(CXXFLAGS) -I../src highscore_store_test.cpp ../src/highscore_store.cpp -o $@

keymap_test: keymap_test.cpp ../src/keymap.cpp ../src/keymap.h ../src/pathutil.h
	$(CXX) $(CXXFLAGS) -I../src keymap_test.cpp ../src/keymap.cpp -o $@

log_timestamp_test: log_timestamp_test.cpp ../src/log_timestamp.h
	$(CXX) $(CXXFLAGS) -I../src log_timestamp_test.cpp -o $@

steam_utils_compat_test: steam_utils_compat_test.cpp ../src/steam_utils_compat.hpp
	$(CXX) $(CXXFLAGS) -I../src steam_utils_compat_test.cpp -o $@

ugc_codec_test: ugc_codec_test.cpp $(CODEC_OBJECTS)
	$(CXX) $(CXXFLAGS) -I../src ugc_codec_test.cpp $(CODEC_OBJECTS) -o $@

$(CODEC_BUILD_DIR)/ugc_codec.o: ../src/ugc_codec.cpp ../src/ugc_codec.h ../src/lz4/lz4frame.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../src -c $< -o $@

$(CODEC_BUILD_DIR)/%.o: ../src/lz4/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../src/lz4 -c $< -o $@

test: $(TARGETS)
	./runtime_config_test
	./highscore_store_test
	./keymap_test
	./log_timestamp_test
	./steam_utils_compat_test
	./ugc_codec_test

clean:
	rm -f $(TARGETS) sound_volume_test
	rm -rf $(CODEC_BUILD_DIR)

# Optional integration test using a built runtime and its vendored mGBA/Steam libraries.
RUNTIME_BUILD ?= ../build/Darwin
STEAM_LIBRARY ?= ../steamworks/redistributable_bin/osx/libsteam_api.dylib
MGBA_LIBRARY ?= $(RUNTIME_BUILD)/mgba/libmgba.dylib
.PHONY: test-sound
test-sound: sound_volume_test
	DYLD_LIBRARY_PATH="$(abspath $(dir $(STEAM_LIBRARY)))" ./sound_volume_test

sound_volume_test: sound_volume_test.cpp $(RUNTIME_BUILD)/dewpoint_runtime.o $(RUNTIME_BUILD)/mgbahelper.o $(RUNTIME_BUILD)/highscore_store.o $(RUNTIME_BUILD)/ugc_codec.o $(MGBA_LIBRARY) $(STEAM_LIBRARY)
	$(CXX) $(CXXFLAGS) -DENABLE_DIRECTORIES -DENABLE_VFS -I../src -isystem ../mgba/include -isystem $(RUNTIME_BUILD)/mgba/include $^ $(RUNTIME_BUILD)/lz4/*.o -Wl,-rpath,$(abspath $(RUNTIME_BUILD)/mgba) -Wl,-rpath,$(abspath $(dir $(STEAM_LIBRARY))) -o $@
