# Dreamlink build. Everything is written under this directory (build/, data/).
# Usage:  make            build simulator and tests
#         make test       build and run unit tests
#         make run-sim    run the simulator with defaults
#         make asan       tests under AddressSanitizer + UBSan
#         make SQLITE=1   also build the SQLite backend (needs libsqlite3-dev)
CXX      ?= g++
BUILD    ?= build
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc
LDLIBS   +=

ifdef SAN
  CXXFLAGS := -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wpedantic
endif

LIB_SRCS := src/json.cpp src/event.cpp src/codec.cpp src/jsonl_store.cpp \
            src/pipeline.cpp src/simulator.cpp
ifeq ($(SQLITE),1)
  LIB_SRCS += src/sqlite_store.cpp
  CPPFLAGS += -DDREAMLINK_WITH_SQLITE
  LDLIBS   += -lsqlite3
endif

LIB_OBJS  := $(LIB_SRCS:src/%.cpp=$(BUILD)/%.o)
SIM_OBJ   := $(BUILD)/sim_main.o
TEST_OBJ  := $(BUILD)/test_main.o
DEPS      := $(LIB_OBJS:.o=.d) $(SIM_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

.PHONY: all test run-sim asan clean
all: $(BUILD)/dreamlink_sim $(BUILD)/dreamlink_tests

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/test_main.o: tests/test_main.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/dreamlink_sim: $(LIB_OBJS) $(SIM_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/dreamlink_tests: $(LIB_OBJS) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

test: $(BUILD)/dreamlink_tests
	DREAMLINK_TEST_TMP=$(BUILD)/test-tmp $(BUILD)/dreamlink_tests

run-sim: $(BUILD)/dreamlink_sim
	$(BUILD)/dreamlink_sim --events 200

asan:
	$(MAKE) BUILD=build-asan SAN=1 test

clean:
	rm -rf build build-asan

-include $(DEPS)
