CFLAGS := -Wall -g
CXXFLAGS := -Wall -std=c++17 -g
CXXLINKLIBS := -lwayland-client -lopencv_core -lopencv_imgproc -linput -ludev

main: build/main.o build/xdg-shell.o build/wlr-layer-shell-unstable-v1.o build/wlr-screencopy-unstable-v1.o build/wlr-virtual-pointer-unstable-v1.o build/xdg-output-unstable-v1.o
	clang++ $(CXXFLAGS) $(CXXLINKLIBS) -o $@ $^

build/main.o: src/main.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/xdg-shell.o: src/xdg-shell.c
	clang $(CFLAGS) -c -o $@ $^

build/wlr-layer-shell-unstable-v1.o: src/wlr-layer-shell-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

build/wlr-screencopy-unstable-v1.o: src/wlr-screencopy-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

build/wlr-virtual-pointer-unstable-v1.o: src/wlr-virtual-pointer-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

build/xdg-output-unstable-v1.o: src/xdg-output-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^
