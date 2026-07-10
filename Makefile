CXX := g++
INCLUDES := -Iinclude

# Detect CPU core count to determine performance tier
NUM_CORES := $(shell nproc)
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -MMD -MP

ifeq ($(shell test $(NUM_CORES) -ge 3; echo $$?),0)
    # Enable busy-waiting and native microarchitecture tuning on systems with >= 3 cores
    CXXFLAGS += -march=native -DPRODUCTION_MODE
endif

# Automatically determine default affinity profile based on CPU core count,
# unless it is already overridden on the command line.
ifndef AFFINITY_PROFILE
    ifeq ($(shell test $(NUM_CORES) -ge 5; echo $$?),0)
        AFFINITY_PROFILE := AFFINITY_PROFILE_ISOLATED
    else ifeq ($(shell test $(NUM_CORES) -ge 4; echo $$?),0)
        AFFINITY_PROFILE := AFFINITY_PROFILE_4CORE
    else ifeq ($(shell test $(NUM_CORES) -ge 3; echo $$?),0)
        AFFINITY_PROFILE := AFFINITY_PROFILE_3CORE
    else
        AFFINITY_PROFILE := AFFINITY_PROFILE_SHARED
    endif
endif

# If AFFINITY_PROFILE is defined (either automatically or manually), compile it in
ifdef AFFINITY_PROFILE
    CXXFLAGS += -D$(AFFINITY_PROFILE)
endif

BUILD_DIR := build
SRC_DIR := src
SERVICE_DIR := app/services
AGENT_DIR := app/client-agents
EXAMPLE_DIR := app/client-examples
CLIENT_PERF_DIR := app/client-perf
COMPONENT_DIR := app/components
TEST_DIR := tests
FBS_DIR := fbs
FBS_OUT := include/fbs
# Choose DB type: SQLITE (default), PGSQL, or CSV
DB_TYPE ?= SQLITE

LDLIBS := -lgtest -lgtest_main -pthread -lrt -lssl -lcrypto -lspdlog -lfmt
TEST_LDLIBS := -lgtest -lgtest_main -pthread -lrt -lssl -lcrypto -lspdlog -lfmt

ifeq ($(DB_TYPE),PGSQL)
    CXXFLAGS += -DUSE_PGSQL
    LDLIBS += -lpqxx -lpq
    TEST_LDLIBS += -lpqxx -lpq
else ifeq ($(DB_TYPE),CSV)
    CXXFLAGS += -DUSE_CSV
else
    CXXFLAGS += -DUSE_SQLITE
    LDLIBS += -lsqlite3
    TEST_LDLIBS += -lsqlite3
endif


# -----------------------------------------------------------------------------
# FlatBuffers
# -----------------------------------------------------------------------------

FBS_SOURCES := $(wildcard $(FBS_DIR)/*.fbs)
FBS_TS_OUT := web/src/fbs
FBS_TS_GENERATED := $(patsubst $(FBS_DIR)/%.fbs,$(FBS_TS_OUT)/%.ts,$(FBS_SOURCES))
FBS_GENERATED := $(patsubst $(FBS_DIR)/%.fbs,$(FBS_OUT)/%_generated.h,$(FBS_SOURCES)) $(FBS_TS_GENERATED)

# -----------------------------------------------------------------------------
# Source Objects
# -----------------------------------------------------------------------------

SRC_SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
ifneq ($(DB_TYPE),PGSQL)
    SRC_SOURCES := $(filter-out $(SRC_DIR)/PostgresClientDatabase.cpp $(SRC_DIR)/PostgresSymbolDatabase.cpp,$(SRC_SOURCES))
endif
ifneq ($(DB_TYPE),SQLITE)
    SRC_SOURCES := $(filter-out $(SRC_DIR)/SQLiteClientDatabase.cpp $(SRC_DIR)/SQLiteSymbolDatabase.cpp,$(SRC_SOURCES))
endif
ifneq ($(DB_TYPE),CSV)
    SRC_SOURCES := $(filter-out $(SRC_DIR)/CSVClientDatabase.cpp $(SRC_DIR)/CSVSymbolDatabase.cpp,$(SRC_SOURCES))
endif
SRC_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_SOURCES))
SRC_DEPS := $(SRC_OBJECTS:.o=.d)

DB_OBJS_ALL := $(BUILD_DIR)/PostgresClientDatabase.o $(BUILD_DIR)/PostgresSymbolDatabase.o \
               $(BUILD_DIR)/SQLiteClientDatabase.o $(BUILD_DIR)/SQLiteSymbolDatabase.o \
               $(BUILD_DIR)/CSVClientDatabase.o $(BUILD_DIR)/CSVSymbolDatabase.o


# -----------------------------------------------------------------------------
# Service Executables
# -----------------------------------------------------------------------------

SERVICE_SOURCES := $(wildcard $(SERVICE_DIR)/*.cpp)
SERVICE_TARGETS := $(patsubst $(SERVICE_DIR)/%.cpp,$(BUILD_DIR)/services/%,$(SERVICE_SOURCES))

# -----------------------------------------------------------------------------
# Agent Executables
# -----------------------------------------------------------------------------

AGENT_SOURCES := $(wildcard $(AGENT_DIR)/*.cpp)
AGENT_TARGETS := $(patsubst $(AGENT_DIR)/%.cpp,$(BUILD_DIR)/client-agents/%,$(AGENT_SOURCES))

# -----------------------------------------------------------------------------
# Example Executables
# -----------------------------------------------------------------------------

EXAMPLE_SOURCES := $(wildcard $(EXAMPLE_DIR)/*.cpp)
EXAMPLE_TARGETS := $(patsubst $(EXAMPLE_DIR)/%.cpp,$(BUILD_DIR)/client-examples/%,$(EXAMPLE_SOURCES))

# -----------------------------------------------------------------------------
# Component Executables
# -----------------------------------------------------------------------------

COMPONENT_SOURCES := $(wildcard $(COMPONENT_DIR)/*.cpp)
COMPONENT_TARGETS := $(patsubst $(COMPONENT_DIR)/%.cpp,$(BUILD_DIR)/components/%,$(COMPONENT_SOURCES))

# -----------------------------------------------------------------------------
# Client Perf Executables
# -----------------------------------------------------------------------------

CLIENT_PERF_SOURCES := $(wildcard $(CLIENT_PERF_DIR)/*.cpp)
CLIENT_PERF_TARGETS := $(patsubst $(CLIENT_PERF_DIR)/%.cpp,$(BUILD_DIR)/client-perf/%,$(CLIENT_PERF_SOURCES))

# -----------------------------------------------------------------------------
# Observability Executables
# -----------------------------------------------------------------------------

OBS_DIR := performance
OBS_SOURCES := $(wildcard $(OBS_DIR)/*.cpp)
OBS_TARGETS := $(patsubst $(OBS_DIR)/%.cpp,$(BUILD_DIR)/performance/%,$(OBS_SOURCES))
EBPF_DIR := performance/ebpf

# -----------------------------------------------------------------------------
# Test Executables
# tests/foo.cpp -> build/tests/foo
# -----------------------------------------------------------------------------

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_TARGETS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/tests/%,$(TEST_SOURCES))

# -----------------------------------------------------------------------------
# Default Target
# -----------------------------------------------------------------------------

WEB_DIR := web

.PHONY: all
all: $(FBS_GENERATED) $(SERVICE_TARGETS) $(AGENT_TARGETS) $(EXAMPLE_TARGETS) $(CLIENT_PERF_TARGETS) $(COMPONENT_TARGETS) $(OBS_TARGETS) ebpf web_target

.PHONY: fbs
fbs: $(FBS_GENERATED)

.PHONY: ebpf
ebpf: $(FBS_GENERATED)
	$(MAKE) -C $(EBPF_DIR)

.PHONY: web_target
web_target: $(FBS_GENERATED)
	$(MAKE) -C $(WEB_DIR)

.PHONY: benchmark
benchmark: $(FBS_GENERATED) $(SERVICE_TARGETS) \
           $(BUILD_DIR)/client-perf/benchmark-trader \
           $(OBS_TARGETS) ebpf

# -----------------------------------------------------------------------------
# Build Services
# -----------------------------------------------------------------------------

$(BUILD_DIR)/services/%: $(SERVICE_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/services
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Agents
# -----------------------------------------------------------------------------

$(BUILD_DIR)/client-agents/%: $(AGENT_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/client-agents
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(filter-out $(DB_OBJS_ALL) $(BUILD_DIR)/ClientManager.o,$(SRC_OBJECTS)) $(TEST_LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Components
# -----------------------------------------------------------------------------

$(BUILD_DIR)/components/%: $(COMPONENT_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/components
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Examples
# -----------------------------------------------------------------------------

$(BUILD_DIR)/client-examples/%: $(EXAMPLE_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/client-examples
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(filter-out $(DB_OBJS_ALL) $(BUILD_DIR)/ClientManager.o,$(SRC_OBJECTS)) $(TEST_LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Client Perf
# -----------------------------------------------------------------------------

$(BUILD_DIR)/client-perf/%: $(CLIENT_PERF_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/client-perf
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(filter-out $(DB_OBJS_ALL) $(BUILD_DIR)/ClientManager.o,$(SRC_OBJECTS)) $(TEST_LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Observabilities
# -----------------------------------------------------------------------------

$(BUILD_DIR)/performance/%: $(OBS_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/performance
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Tests
# -----------------------------------------------------------------------------

$(BUILD_DIR)/tests/%: $(TEST_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/tests
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(filter-out $(DB_OBJS_ALL) $(BUILD_DIR)/ClientManager.o $(BUILD_DIR)/AlgoTradingClient.o,$(SRC_OBJECTS)) $(TEST_LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Run Tests
# -----------------------------------------------------------------------------

.PHONY: test
test: $(TEST_TARGETS)
	@for test_bin in $(TEST_TARGETS); do \
		echo "Running $$test_bin"; \
		$$test_bin || exit $$?; \
	done
	@for test in tests/*; do \
		if [ -d "$$test" ]; then \
			echo "Running Test: $$test"; \
			./scripts/test-correctness "$$test" || exit $$?; \
		fi \
	done

# -----------------------------------------------------------------------------
# Compile Source Objects
# -----------------------------------------------------------------------------

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# -----------------------------------------------------------------------------
# Generate FlatBuffers Headers
# -----------------------------------------------------------------------------

$(FBS_OUT)/%_generated.h: $(FBS_DIR)/%.fbs
	@mkdir -p $(FBS_OUT)
	flatc --cpp --gen-mutable --gen-object-api -o $(FBS_OUT) $<

$(FBS_TS_OUT)/%.ts: $(FBS_DIR)/%.fbs
	@mkdir -p $(FBS_TS_OUT)
	flatc --ts -o $(FBS_TS_OUT) $<

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(FBS_OUT)
	rm -rf $(FBS_TS_OUT)
	$(MAKE) -C $(EBPF_DIR) clean
	$(MAKE) -C $(WEB_DIR) clean

# -----------------------------------------------------------------------------
# Include Dependency Files
# -----------------------------------------------------------------------------

-include $(SRC_DEPS)