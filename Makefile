CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Iinclude

BUILD := build
LIB_SRCS := src/ast.cpp src/lexer.cpp src/registry.cpp src/facet.cpp src/sympy.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(BUILD)/%.o)
TEST_BIN := $(BUILD)/test_facet
CLI_BIN := $(BUILD)/facet

.PHONY: all test clean

all: $(TEST_BIN) $(CLI_BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp include/facet/facet.hpp src/facet_internal.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): tests/test_facet.cpp $(LIB_OBJS) include/facet/facet.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) tests/test_facet.cpp $(LIB_OBJS) -o $@

$(CLI_BIN): src/facet_cli.cpp $(LIB_OBJS) include/facet/facet.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) src/facet_cli.cpp $(LIB_OBJS) -o $@

test: $(TEST_BIN) $(CLI_BIN)
	$(TEST_BIN)
	sh tests/test_cli.sh $(CLI_BIN)

clean:
	rm -rf $(BUILD)
