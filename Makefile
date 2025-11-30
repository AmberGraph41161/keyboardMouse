CXXFLAGS := -Wall -std=c++17
CXXLINKLIBS := -lwayland-client

main: build/main.o build/xdg-shell.o
	clang++ $(CXXFLAGS) $(CXXLINKLIBS) -o $@ $^

build/main.o: src/main.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/xdg-shell.o: src/xdg-shell.c
	clang -Wall -c -o $@ $^
