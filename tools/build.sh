#!/bin/sh
# Збирання прошивок — те саме, що tools/build.py (Windows, Linux, macOS): tools/build.sh [all|mist|mayak|bms|--setup]
exec python3 "$(dirname "$0")/build.py" "$@"
