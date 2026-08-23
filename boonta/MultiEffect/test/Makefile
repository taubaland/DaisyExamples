# Host-side tests for the MultiEffect DSP.
#
# PedalState, Chain and the three effects deliberately know nothing about
# libDaisy, so they build and run on the machine you are sitting at -- no board,
# no toolchain, no flashing. Controls and LedView do touch libDaisy and are not
# covered here.
#
#   make        build the test binary
#   make run    build and run the checks
#   make decay  build and run the reverb decay measurement
#
# Any host compiler will do. clang++ is the default because it is what ships
# with the Windows toolchain most Daisy setups already have; override it with
# `make CXX=g++` if you have something else.

# Assigned with = rather than ?= because make has a built-in default for CXX,
# which ?= would defer to -- and on a Daisy setup that default is the ARM cross
# compiler. A command-line `make CXX=g++` still overrides this.
CXX = clang++

CXXFLAGS = -std=c++14 -O2 -Wall

DAISYSP_DIR = ../../../DaisySP

INCLUDES = -Istubs -I.. -I$(DAISYSP_DIR)/Source

# The libDaisy-free half of the pedal. Chain pulls in the three effects.
DSP_SOURCES = \
../Chain.cpp \
../EqEffect.cpp \
../DriveEffect.cpp \
../ReverbEffect.cpp \
../PedalState.cpp

BUILD_DIR = build

ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif

.PHONY: all run decay clean

all: $(BUILD_DIR)/test$(EXE)

run: $(BUILD_DIR)/test$(EXE)
	@./$(BUILD_DIR)/test$(EXE)

decay: $(BUILD_DIR)/decay$(EXE)
	@./$(BUILD_DIR)/decay$(EXE)

$(BUILD_DIR)/test$(EXE): test.cpp $(DSP_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR)/decay$(EXE): decay.cpp $(DSP_SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
