# 宿主机测试（macOS/Linux），不需要 devkitPro
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra
SANFLAGS ?= -fsanitize=address,undefined
TSANFLAGS?= -fsanitize=thread

BINS = test_tracker test_seqlock test_reloc

.PHONY: all test tsan clean
all: $(BINS)

test_tracker: test_tracker.cpp ../plugin/source/tracker.hpp ../shared/hud_protocol.h
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_tracker.cpp

test_seqlock: test_seqlock.cpp ../plugin/source/tracker.hpp ../overlay/source/hud_reader.hpp ../shared/hud_protocol.h
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -pthread -o $@ test_seqlock.cpp

test_reloc: test_reloc.cpp ../plugin/source/arm64_reloc.hpp
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ test_reloc.cpp

test: $(BINS)
	@for b in $(BINS); do echo "--- $$b ---"; ./$$b || exit 1; done

# ThreadSanitizer 单独一轮：ASan 与 TSan 不能同时开
tsan: test_seqlock.cpp
	$(CXX) $(CXXFLAGS) $(TSANFLAGS) -pthread -o test_seqlock_tsan test_seqlock.cpp
	./test_seqlock_tsan

clean:
	rm -rf $(BINS) test_seqlock_tsan *.dSYM gt.o gt.s
