#!/bin/sh
# Прошивка кабелем — те саме, що tools/flash.py (Windows, Linux, macOS): tools/flash.sh mist|mayak|bms [--port …] [--new]
D="$(dirname "$0")"
PY=python3; [ -x "$D/../venv/bin/python" ] && PY="$D/../venv/bin/python"      # venv з pyserial, якщо є
exec "$PY" "$D/flash.py" "$@"
