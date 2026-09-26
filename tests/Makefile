# 宿主机测试（macOS/Linux），不需要 devkitPro
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra
SANFLAGS ?= -fsanitize=address,undefined

BINS = test_hud_model test_toggles test_map_timing test_map_refresh_plan test_options_menu_model

.PHONY: all test clean
all: $(BINS)

test_hud_model: test_hud_model.cpp ../exl/program/hud_model.hpp ../exl/program/hud_phase_model.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_hud_model.cpp

test_toggles: test_toggles.cpp ../exl/program/toggles_model.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_toggles.cpp

test_map_timing: test_map_timing.cpp ../exl/program/map_timing.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_map_timing.cpp

test_map_refresh_plan: test_map_refresh_plan.cpp ../exl/program/map_refresh_plan.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_map_refresh_plan.cpp

test_options_menu_model: test_options_menu_model.cpp ../exl/program/options_menu_model.hpp ../exl/program/toggles_model.hpp ../exl/program/start_crest_model.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_options_menu_model.cpp

test: $(BINS)
	@for b in $(BINS); do echo "--- $$b ---"; ./$$b || exit 1; done

clean:
	rm -rf $(BINS) *.dSYM
