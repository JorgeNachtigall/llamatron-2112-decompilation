#!/bin/sh
# Run a tools/ghidra script against the headless project ghidra/headless/LlamatronCLI.gpr.
# Never point this at ghidra/Llamatron.gpr: that GUI project belongs to the user (and is locked).
#
#   tools/ghidra/run_headless.sh <program|all> <Script.java> [script args...]
#   e.g. tools/ghidra/run_headless.sh game86.exe ApplySymbols.java re/symbols.csv types
#        tools/ghidra/run_headless.sh all Export.java re/listings
#
# GHIDRA_HOME defaults to the Homebrew install; relative script args are made absolute.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -ge 2 ] || { sed -n '2,9p' "$0"; exit 2; }
PROG=$1; SCRIPT=$2; shift 2
if [ -z "${GHIDRA_HOME:-}" ]; then
	GHIDRA_HOME=$(ls -d /opt/homebrew/Cellar/ghidra/*/libexec 2>/dev/null | sort -V | tail -1)
fi
HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"
[ -x "$HEADLESS" ] || { echo "analyzeHeadless not found; set GHIDRA_HOME" >&2; exit 1; }
PROJ_DIR="$ROOT/ghidra/headless"
[ -f "$PROJ_DIR/LlamatronCLI.gpr" ] || { echo "missing $PROJ_DIR/LlamatronCLI.gpr" >&2; exit 1; }
ARGS=""
for a in "$@"; do
	case $a in
		/*|dry|types) ;;
		*) [ -e "$ROOT/$a" ] && a="$ROOT/$a" ;;
	esac
	ARGS="$ARGS $a"
done
if [ "$PROG" = all ]; then
	set -- -process
else
	set -- -process "$PROG"
fi
# shellcheck disable=SC2086
exec "$HEADLESS" "$PROJ_DIR" LlamatronCLI "$@" -noanalysis \
	-scriptPath "$ROOT/tools/ghidra" -postScript "$SCRIPT" $ARGS
