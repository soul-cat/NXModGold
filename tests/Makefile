# 宿主机测试（macOS/Linux），不需要 devkitPro
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined

.PHONY: all test clean
all: test_tracker

test_tracker: test_tracker.cpp ../plugin/source/tracker.hpp ../shared/hud_protocol.h
	$(CXX) $(CXXFLAGS) -o $@ test_tracker.cpp

test: test_tracker
	./test_tracker

clean:
	rm -rf test_tracker test_tracker.dSYM
