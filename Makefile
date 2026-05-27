CXX      := g++
CXXFLAGS := -std=c++17 -Wall -g -Itool
LDFLAGS  := -ljsoncpp -lmysqlclient -lboost_system -lpthread

.PHONY: test server clean

test: test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	./$@

server: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f test server
