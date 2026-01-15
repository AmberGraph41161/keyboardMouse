CFLAGS := -Wall -g
CXXFLAGS := -Wall -std=c++17 -g
CXXLINKLIBS := -lwayland-client -lopencv_core -lopencv_imgproc

keyboardMouse: build/main.o build/virtualInputDevice.o build/keyboard.o build/mouse.o build/absolutePointer.o build/xdg-shell.o build/wlr-layer-shell-unstable-v1.o build/wlr-screencopy-unstable-v1.o build/xdg-output-unstable-v1.o
	clang++ $(CXXFLAGS) $(CXXLINKLIBS) -o $@ $^

build/main.o: src/main.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/virtualInputDevice.o: src/virtualInputDevice.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/keyboard.o: src/keyboard.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/mouse.o: src/mouse.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/absolutePointer.o: src/absolutePointer.cpp
	clang++ $(CXXFLAGS) -c -o $@ $^

build/xdg-shell.o: src/xdg-shell.c
	clang $(CFLAGS) -c -o $@ $^

build/wlr-layer-shell-unstable-v1.o: src/wlr-layer-shell-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

build/wlr-screencopy-unstable-v1.o: src/wlr-screencopy-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

build/xdg-output-unstable-v1.o: src/xdg-output-unstable-v1.c
	clang $(CFLAGS) -c -o $@ $^

.PHONY: opencv
opencv: testing/opencv.cpp
	clang++ -Wall -std=c++17 -g -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -o opencv testing/opencv.cpp

.PHONY: evdev
evdev: testing/evdev.cpp
	clang++ -Wall -std=c++17 -g -o evdev testing/evdev.cpp

.PHONY: clean
clean:
	-rm build/*.o
