#!/usr/bin/env python3
"""Validate re/symbols.csv (see tools/ghidra/README.md).

Checks: header, column count, program/type/dtype values, seg:off syntax, naming rules
(docs/PLAN.md section 4), duplicate addresses per program/type and duplicate names per program.
Exit status 1 when an error was found; warnings do not fail.
"""
import csv
import re
import sys

PROGRAMS = {"game86", "fs", "llama", "install"}
TYPES = {"func", "data", "label"}
DTYPE = re.compile(r"^(byte|char|word|dword|ptr16|farptr)(\[(0x[0-9a-fA-F]+|\d+)\])?$")
ADDR = re.compile(r"^[0-9a-f]{4}:[0-9a-f]{4}$")
NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DATA_PREFIX = ("g_", "tbl_", "s_", "cs_", "cs1_", "smc_", "font_")


def main(path):
    errors = warnings = 0
    seen_addr, seen_name = {}, {}
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    if not rows or rows[0][:5] != ["program", "address", "name", "type", "comment"]:
        print(f"{path}:1: bad header {rows[0] if rows else ''}")
        return 1

    def err(ln, msg):
        nonlocal errors
        errors += 1
        print(f"{path}:{ln}: error: {msg}")

    def warn(ln, msg):
        nonlocal warnings
        warnings += 1
        print(f"{path}:{ln}: warning: {msg}")

    for ln, r in enumerate(rows[1:], start=2):
        if not r or r[0].startswith("#"):
            continue
        if len(r) < 4 or len(r) > 6:
            err(ln, f"expected 4..6 columns, got {len(r)}")
            continue
        prog, addr, name, typ = r[0], r[1], r[2], r[3]
        dtype = r[5] if len(r) > 5 else ""
        if prog not in PROGRAMS:
            err(ln, f"unknown program {prog!r}")
        if not ADDR.match(addr):
            err(ln, f"address {addr!r} is not lower-case seg:off")
        if typ not in TYPES:
            err(ln, f"unknown type {typ!r}")
        if not NAME.match(name):
            err(ln, f"invalid identifier {name!r}")
        if name.startswith(("FUN_", "LAB_", "DAT_", "SUB_")):
            err(ln, f"{name}: Ghidra default prefixes are reserved")
        if dtype and (typ != "data" or not DTYPE.match(dtype)):
            err(ln, f"bad dtype {dtype!r} for type {typ}")
        if typ == "data" and not name.startswith(DATA_PREFIX):
            warn(ln, f"data name {name} lacks one of {DATA_PREFIX}")
        if typ == "func" and name.startswith(DATA_PREFIX[:3]):
            warn(ln, f"function named like data: {name}")
        key = (prog, addr, typ)
        if key in seen_addr:
            err(ln, f"duplicate {typ} at {prog} {addr} (line {seen_addr[key]})")
        seen_addr[key] = ln
        nkey = (prog, name)
        if nkey in seen_name:
            err(ln, f"duplicate name {name} in {prog} (line {seen_name[nkey]})")
        seen_name[nkey] = ln
    print(f"{path}: {len(rows) - 1} rows, {errors} errors, {warnings} warnings")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "re/symbols.csv"))
