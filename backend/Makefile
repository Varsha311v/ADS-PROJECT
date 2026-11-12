CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS  ?= -pthread
SRC      := src/graph.cpp src/json.cpp src/http.cpp src/store.cpp
BIN      := build/friend-finder

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(SRC) src/main.cpp $(wildcard src/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(SRC) src/main.cpp -o $@ $(LDFLAGS)

build/unit-tests: tests/test_main.cpp src/graph.cpp src/json.cpp src/http.cpp $(wildcard src/*.hpp) | build
	$(CXX) $(CXXFLAGS) tests/test_main.cpp src/graph.cpp src/json.cpp src/http.cpp -o $@ $(LDFLAGS)

test: build/unit-tests
	./build/unit-tests

run: $(BIN)
	./$(BIN) --static .. --data data/network.json

clean:
	rm -rf build

.PHONY: all test run clean
