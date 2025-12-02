CXXFLAGS := -Wall -std=c++17
CXXLINKLIBS := -lwayland-client -lopencv_core -lopencv_imgproc

main: build/main.o build/xdg-shell.o build/wlr-layer-shell-unstable-v1.o build/wlr-screencopy-unstable-v1.o
	clang++ $(CXXFLAGS) $(CXXLINKLIBS) -o $@ $^

build/main.o: src/main.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/xdg-shell.o: src/xdg-shell.c
	clang -Wall -c -o $@ $^

build/wlr-layer-shell-unstable-v1.o: src/wlr-layer-shell-unstable-v1.c
	clang -Wall -c -o $@ $^

build/wlr-screencopy-unstable-v1.o: src/wlr-screencopy-unstable-v1.c
	clang -Wall -c -o $@ $^
