# Build gzif against whatever gz-transport your pkg-config can find.
# Override for a different major:  make PKG=gz-transport15
PKG ?= gz-transport13
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall
LDFLAGS ?=

gzif: gzif.cc
	$(CXX) $(CXXFLAGS) $< -o $@ $(shell pkg-config --cflags --libs $(PKG)) $(LDFLAGS)

clean:
	rm -f gzif

.PHONY: clean
