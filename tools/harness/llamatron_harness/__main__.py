"""CLI: run one original routine and print what it changed.

    tools/harness/.venv/bin/python -m llamatron_harness 1000:e4de \\
        --ds 39e9=ff01 --reg bx=1234 [--far] [--ticks N] [--image PATH]

--ds OFF=HEX writes bytes into DGROUP, --mem SEG:OFF=HEX anywhere, --reg NAME=VALUE
sets registers.  Output: registers that changed, DGROUP/memory diff, INT and port log.
"""

import argparse
import sys

from .runner import HarnessError, Machine, parse_addr


def _kv(text: str) -> tuple[str, str]:
    key, _, value = text.partition("=")
    if not value:
        raise argparse.ArgumentTypeError(f"expected KEY=VALUE, got {text!r}")
    return key, value


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="llamatron_harness", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("entry", help="routine address, e.g. 1000:e4de")
    ap.add_argument("--image", help="game86 image (default: see default_game86_path)")
    ap.add_argument("--far", action="store_true", help="routine ends with RETF")
    ap.add_argument("--ds", type=_kv, action="append", default=[], metavar="OFF=HEX")
    ap.add_argument("--mem", type=_kv, action="append", default=[], metavar="SEG:OFF=HEX")
    ap.add_argument("--reg", type=_kv, action="append", default=[], metavar="NAME=VALUE")
    ap.add_argument("--ticks", type=lambda v: int(v, 0), help="BIOS tick count at 0000:046C")
    ap.add_argument("--until", help="run inline code until this address instead of to RET")
    ap.add_argument("--max-insns", type=int, default=5_000_000)
    args = ap.parse_args(argv)

    m = Machine.from_game86(args.image)
    for off, data in args.ds:
        m.write_dgroup(int(off, 16), bytes.fromhex(data))
    for addr, data in args.mem:
        m.write(*parse_addr(addr), bytes.fromhex(data))
    if args.ticks is not None:
        m.set_bios_ticks(args.ticks)
    regs = {name.lower(): int(value, 16) for name, value in args.reg}
    try:
        if args.until:
            res = m.run_until(args.entry, args.until, regs=regs, max_insns=args.max_insns)
        else:
            res = m.call(args.entry, regs=regs, far=args.far, max_insns=args.max_insns)
    except HarnessError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    changed = {k: (res.regs_in[k], v) for k, v in res.regs.items()
               if res.regs_in[k] != v and k not in ("ip", "sp", "cs")}
    print("regs:", " ".join(f"{k}={a:04x}->{b:04x}" for k, (a, b) in changed.items()) or "-")
    print(f"dgroup ({m.dgroup:04x}) diff:")
    for off in sorted(res.dgroup_diff):
        old, new = res.dgroup_diff[off]
        print(f"  [{off:04x}] {old:02x} -> {new:02x}")
    outside = [d for d in res.memory_diff
               if not 0 <= d.start - m.dgroup * 16 < 0x10000]
    for d in outside:
        print(f"  mem {d}")
    for ev in res.interrupts:
        print(f"  INT {ev.number:02x}h AX={ev.regs['ax']:04x}")
    for ev in res.ports:
        print(f"  {ev.direction} {ev.port:04x} = {ev.value:0{ev.size * 2}x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
