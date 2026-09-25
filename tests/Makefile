# 宿主机测试（macOS/Linux），不需要 devkitPro
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra
SANFLAGS ?= -fsanitize=address,undefined

BINS = test_hud_model test_toggles test_map_timing

.PHONY: all test clean
all: $(BINS)

test_hud_model: test_hud_model.cpp ../exl/program/hud_model.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_hud_model.cpp

test_toggles: test_toggles.cpp ../exl/program/toggles_model.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_toggles.cpp

test_map_timing: test_map_timing.cpp ../exl/program/map_timing.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_map_timing.cpp

test: $(BINS)
	@for b in $(BINS); do echo "--- $$b ---"; ./$$b || exit 1; done

clean:
	rm -rf $(BINS) *.dSYM
