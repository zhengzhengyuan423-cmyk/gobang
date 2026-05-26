CXX      := g++
CXXFLAGS := -std=c++17 -Wall -g
LDFLAGS  := -ljsoncpp -lmysqlclient -lboost_system -lpthread

.PHONY: test clean

test: test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	./$@

clean:
	rm -f test
