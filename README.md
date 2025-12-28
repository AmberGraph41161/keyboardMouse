# keyboardMouse

> Use the keyboard to do mouse things

A project inspired by:

- [KeyNav](https://youtu.be/Uot3Cs9YwOA?si=AsGqJGUqo0MNpQzo)
- [mouseless](https://youtu.be/J0rwQVNQkHM?si=v2O7zazIbpw5QAaJ)
- [neverclick](https://www.youtube.com/watch?v=7fGB-hjc2Gc&t=2h00m55s)

# Resources, documentation, and reference material used to help me build this project

- [wayland-book](https://wayland-book.com/introduction.html) for helping me get started on anything wayland client related
- [Learn Wayland by writing a GUI from scratch](https://gaultier.github.io/blog/wayland_from_scratch.html#what-do-we-need) for helping me start understanding the wayland protocol
- [wayland explorer](https://wayland.app/protocols/) for libwayland documentation
- [wayland documentation](https://people.collabora.com/~mvlad/wayland_hawkmoth/index.html) similar help as wayland explorer
    - [ext-image-copy-capture-v1](https://wayland.app/protocols/ext-image-copy-capture-v1)
    - [wlr-screencopy-unstable-v1](https://wayland.app/protocols/wlr-screencopy-unstable-v1)
    - [wlr-layer-shell-unstable-v1](https://wayland.app/protocols/wlr-layer-shell-unstable-v1)
    - [wlr-virtual-pointer-unstable-v1](https://wayland.app/protocols/wlr-virtual-pointer-unstable-v1)
    - [wlr-virtual-pointer-unstable-v1 (wlroots virtual pointer example)](https://git.nixnet.services/blankie/wlroots/src/commit/2382684e942c7361197306862984de5598f7fc30/examples/virtual-pointer.c)
    - [xdg-output-unstable-v1](https://wayland.app/protocols/xdg-output-unstable-v1)
- [grim](https://gitlab.freedesktop.org/emersion/grim) for showing me how to do zwlr_screencopy_manager_v1 stuff
- [tofi](https://github.com/philj56/tofi) and [fuzzel](https://codeberg.org/dnkl/fuzzel) for showing me how to draw things wayland-client related
- [YouTube](https://www.youtube.com/)
    - Wayland related
        - [Wayland client basics How to natively speak Wayland in your application, from the bottom up](https://www.youtube.com/watch?v=KbryyNrMYl4)
        - [Writing a Wayland client from scratch! How hard can it be? [minimal native Application]](https://www.youtube.com/watch?v=lw4P1Oup5LQ)
        - [Our wayland client actually works!](https://www.youtube.com/watch?v=13ubS803Q_8)
        - [zero to window - wayland client](https://youtu.be/iIVIu7YRdY0?si=5fj2aMNweNgjqxED)
        - [How the heck does wayland work?](https://www.youtube.com/watch?v=m39KIio5lL4)
    - OpenCV related
        - [Object Detection using HSV Color Space [C++/OpenCV]](https://www.youtube.com/watch?v=vIrmMAib7Go)
        - [How I animate stuff on Desmos Graphing Calculator](https://www.youtube.com/watch?v=BQvBq3K50u8&t=4m21s)
- [OpenCV forums "how to group contours in close proximity to each other"](https://forum.opencv.org/t/how-to-group-contours-in-close-proximity-to-each-other/1719)
- [OpenCV Documentation dilation](https://docs.opencv.org/4.x/d4/d86/group__imgproc__filter.html#ga4ff0f3318642c4f469d0e11f242f3b6c)
- [OpenCV Documentation Morphological Transformations](https://docs.opencv.org/4.x/d9/d61/tutorial_py_morphological_ops.html)
- [OpenCV Documentation drawing functions](https://docs.opencv.org/4.x/d6/d6e/group__imgproc__draw.html#ga3d2abfcb995fd2db908c8288199dba82)
- [stackoverflow](https://stackoverflow.com)
    - [Linux: Canceling input from /dev/input/event\*](https://stackoverflow.com/questions/68713392/linux-canceling-input-from-dev-input-event) for showing me how to 'eat' key events
    - [input\_event structure description (from linux/input.h)](https://stackoverflow.com/questions/16695432/input-event-structure-description-from-linux-input-h)
    - [Why does \`ioctl(fd, EVIOCGRAB, 1)\` cause key spam sometimes?](https://stackoverflow.com/questions/41995349/why-does-ioctlfd-eviocgrab-1-cause-key-spam-sometimes) for giving me ideas on how to work around this
- [Linux Kernel Documentation, Input event codes](https://docs.kernel.org/input/event-codes.html) for documentation on evdev and other related stuff

# Please view only

This project is available for **viewing only**.
You are welcome to read and explore the source code, but **you may not copy, modify, or distribute** the code under any circumstances.
All rights are reserved to the original author.

For any requests regarding usage, please contact the author directly.
