#!/bin/sh

wayland-scanner private-code  < wayland-protocols/xdg-shell.xml > "src/wayland/protocols/xdg-shell.c"
wayland-scanner client-header < wayland-protocols/xdg-shell.xml > "src/wayland/protocols/xdg-shell.h"

wayland-scanner private-code  < wayland-protocols/cursor-shape-v1.xml > "src/wayland/protocols/cursor-shape.c"
wayland-scanner client-header < wayland-protocols/cursor-shape-v1.xml > "src/wayland/protocols/cursor-shape.h"

wayland-scanner private-code  < wayland-protocols/tablet-v2.xml > "src/wayland/protocols/tablet.c"
wayland-scanner client-header < wayland-protocols/tablet-v2.xml > "src/wayland/protocols/tablet.h"

wayland-scanner private-code  < wayland-protocols/xdg-decoration-unstable-v1.xml > "src/wayland/protocols/xdg-decoration.c"
wayland-scanner client-header < wayland-protocols/xdg-decoration-unstable-v1.xml > "src/wayland/protocols/xdg-decoration.h"

