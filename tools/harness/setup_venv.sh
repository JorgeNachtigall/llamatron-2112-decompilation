#!/bin/sh
# Create tools/harness/.venv (gitignored) with the test-oracle dependencies.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
python=${PYTHON:-python3}
[ -x "$here/.venv/bin/python" ] || "$python" -m venv "$here/.venv"
"$here/.venv/bin/python" -m pip install -q -r "$here/requirements.txt"
echo "$here/.venv/bin/python"
