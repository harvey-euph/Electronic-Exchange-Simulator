CXX := g++
INCLUDES := -Iinclude

# Detect CPU core count to determine performance tier
NUM_CORES := $(shell nproc)
CXXFLAGS := -std=c++20 -O3 -Wall -Wextra -MMD -MP

ifeq ($(shell test $(NUM_CORES) -ge 8; echo $$?),0)
    # Enable busy-waiting and native microarchitecture tuning on systems with >= 8 cores
    CXXFLAGS += -march=native -DPRODUCTION_MODE
endif

# Automatically determine default affinity profile based on CPU core count,
# unless it is already overridden on the command line.
ifndef AFFINITY_PROFILE
    ifeq ($(shell test $(NUM_CORES) -ge 6; echo $$?),0)
        AFFINITY_PROFILE := AFFINITY_PROFILE_ISOLATED
    else ifeq ($(shell test $(NUM_CORES) -ge 4; echo $$?),0)
        AFFINITY_PROFILE := AFFINITY_PROFILE_COMPACT
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
SERVICE_DIR := service
TEST_DIR := tests
FBS_DIR := fbs
FBS_OUT := include/fbs
LDLIBS := -lgtest -lgtest_main -pthread

# -----------------------------------------------------------------------------
# FlatBuffers
# -----------------------------------------------------------------------------

FBS_SOURCES := $(wildcard $(FBS_DIR)/*.fbs)
FBS_GENERATED := $(patsubst $(FBS_DIR)/%.fbs,$(FBS_OUT)/%_generated.h,$(FBS_SOURCES))

# -----------------------------------------------------------------------------
# Source Objects
# -----------------------------------------------------------------------------

SRC_SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
SRC_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_SOURCES))
SRC_DEPS := $(SRC_OBJECTS:.o=.d)

# -----------------------------------------------------------------------------
# Service Executables
# -----------------------------------------------------------------------------

SERVICE_SOURCES := $(wildcard $(SERVICE_DIR)/*.cpp)
SERVICE_TARGETS := $(patsubst $(SERVICE_DIR)/%.cpp,$(BUILD_DIR)/service/%,$(SERVICE_SOURCES))

APP_TARGETS := $(SERVICE_TARGETS)

# -----------------------------------------------------------------------------
# Observability Executables
# -----------------------------------------------------------------------------

OBS_DIR := observabilities
OBS_SOURCES := $(wildcard $(OBS_DIR)/*.cpp)
OBS_TARGETS := $(patsubst $(OBS_DIR)/%.cpp,$(BUILD_DIR)/observabilities/%,$(OBS_SOURCES))
EBPF_DIR := observabilities/ebpf

# -----------------------------------------------------------------------------
# Test Executables
# tests/foo.cpp -> build/tests/foo
# -----------------------------------------------------------------------------

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_TARGETS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/tests/%,$(TEST_SOURCES))

# -----------------------------------------------------------------------------
# Default Target
# -----------------------------------------------------------------------------

.PHONY: all
all: $(FBS_GENERATED) $(APP_TARGETS) $(OBS_TARGETS) ebpf_target

.PHONY: ebpf_target
ebpf_target: $(FBS_GENERATED)
	$(MAKE) -C $(EBPF_DIR)

# -----------------------------------------------------------------------------
# Build Services
# -----------------------------------------------------------------------------

$(BUILD_DIR)/service/client/%: $(SERVICE_DIR)/client/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/service/client
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

$(BUILD_DIR)/service/%: $(SERVICE_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/service
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Observabilities
# -----------------------------------------------------------------------------

$(BUILD_DIR)/observabilities/%: $(OBS_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/observabilities
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Build Tests
# -----------------------------------------------------------------------------

$(BUILD_DIR)/tests/%: $(TEST_DIR)/%.cpp $(SRC_OBJECTS) $(FBS_GENERATED)
	@mkdir -p $(BUILD_DIR)/tests
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(SRC_OBJECTS) $(LDLIBS) -o $@

# -----------------------------------------------------------------------------
# Run Tests
# -----------------------------------------------------------------------------

.PHONY: test
test: $(TEST_TARGETS)
	@for test_bin in $(TEST_TARGETS); do \
		echo "Running $$test_bin"; \
		$$test_bin || exit $$?; \
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

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(FBS_OUT)
	$(MAKE) -C $(EBPF_DIR) clean

# -----------------------------------------------------------------------------
# Include Dependency Files
# -----------------------------------------------------------------------------

-include $(SRC_DEPS)