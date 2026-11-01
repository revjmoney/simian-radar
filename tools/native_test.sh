#!/bin/sh
# Host unit tests. Uses the MSYS2 MinGW g++ on this PC (not on PATH by default).
export PATH="/d/onerous/msys64/mingw64/bin:$PATH"
exec pio test -e native "$@"
