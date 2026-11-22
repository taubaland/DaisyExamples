# Host-side tests for Boonta Elements.
#
# PedalState and Effect know nothing about libDaisy, and upstream's DSP was
# written to build on a host as well as on the module (that is what its TEST
# define is for), so the whole sounding half of the pedal builds and runs on
# the machine you are sitting at. Controls, LedView, MidiControl and Samples
# touch libDaisy and are not covered here.
#
#   make        build the test binary
#   make run    build and run the checks
#
# Assigned with = rather than ?= because make has a built-in default for CXX,
# which ?= would defer to -- and on a Daisy setup that default is the ARM
# cross compiler. A command-line `make CXX=g++` still overrides this.
CXX = clang++

# TEST selects stmlib's portable code paths over its ARM assembly.
# _USE_MATH_DEFINES is for clang on Windows, whose <cmath> is Microsoft's and
# keeps M_PI behind it; everywhere else it is harmless.
CXXFLAGS = -std=c++14 -O2 -Wall -DTEST -D_USE_MATH_DEFINES -Wno-unused-local-typedef

# `..` for the pedal's own headers and the vendored elements/ tree, and the
# top of the repository for stmlib/.
INCLUDES = -I.. -I../../..

# On the host the sample tables are simply linked in: resources.cc is
# compiled as it stands, with none of the renaming the firmware does.
SOURCES = \
test.cpp \
../Effect.cpp \
../PedalState.cpp \
../ElementsDsp.cpp \
resources.cpp

BUILD_DIR = build

ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif

.PHONY: all run clean

all: $(BUILD_DIR)/test$(EXE)

run: $(BUILD_DIR)/test$(EXE)
	@./$(BUILD_DIR)/test$(EXE)

$(BUILD_DIR)/test$(EXE): $(SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
