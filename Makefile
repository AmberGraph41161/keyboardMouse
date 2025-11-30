CXXFLAGS := -Wall -std=c++17

main: src/main.cpp
	clang++ $(CXXFLAGS) -o $@ $^
