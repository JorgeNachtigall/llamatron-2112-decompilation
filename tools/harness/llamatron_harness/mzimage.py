"""Load an (unpacked) DOS executable into a real-mode memory image.

Test-oracle code only (docs/PLAN.md §0.1): nothing here is ever linked into src/.

Two input forms are accepted:
  * a plain MZ executable (e.g. unpacked/game86.exe after unlzexe + unexepack):
    the header is parsed and every relocation gets the load segment added;
  * a flat load image whose relocations were already applied for segment 0x1000
    (what tools/extract may write to build/generated/exe/*.unpacked).

The default load segment is 0x1000 so that Ghidra addresses (image base 1000)
equal real-mode seg:off addresses in the emulator.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

PARAGRAPH = 16
DEFAULT_LOAD_SEG = 0x1000


@dataclass
class LoadedImage:
    load_seg: int
    data: bytes                 # load module, relocated, without MZ header
    min_extra_paras: int        # MZ minalloc (BSS after the load module), 0 for flat images
    entry_cs: int               # absolute segment
    entry_ip: int
    stack_ss: int               # absolute segment
    stack_sp: int
    relocations: list[tuple[int, int]] = field(default_factory=list)  # (seg rel, off)

    @property
    def base(self) -> int:
        return self.load_seg * PARAGRAPH

    @property
    def end(self) -> int:
        """Linear address just past the load module plus its minimum BSS."""
        return self.base + len(self.data) + self.min_extra_paras * PARAGRAPH


def _parse_mz(raw: bytes, load_seg: int) -> LoadedImage:
    (magic, cblp, cp, crlc, cparhdr, minalloc, _maxalloc, ss, sp, _csum, ip, cs,
     lfarlc, _ovno) = struct.unpack_from("<14H", raw, 0)
    if magic not in (0x5A4D, 0x4D5A):
        raise ValueError("not an MZ executable")
    file_size = cp * 512 - ((512 - cblp) if cblp else 0)
    header_size = cparhdr * PARAGRAPH
    module = bytearray(raw[header_size:file_size])
    relocs = []
    for i in range(crlc):
        off, seg = struct.unpack_from("<HH", raw, lfarlc + 4 * i)
        where = seg * PARAGRAPH + off
        if where + 2 > len(module):
            raise ValueError(f"relocation {seg:04x}:{off:04x} outside load module")
        value = struct.unpack_from("<H", module, where)[0]
        struct.pack_into("<H", module, where, (value + load_seg) & 0xFFFF)
        relocs.append((seg, off))
    return LoadedImage(load_seg=load_seg, data=bytes(module), min_extra_paras=minalloc,
                       entry_cs=(cs + load_seg) & 0xFFFF, entry_ip=ip,
                       stack_ss=(ss + load_seg) & 0xFFFF, stack_sp=sp, relocations=relocs)


def load_image(path: str | Path, load_seg: int = DEFAULT_LOAD_SEG,
               flat_entry: tuple[int, int] | None = None,
               flat_stack: tuple[int, int] | None = None) -> LoadedImage:
    """Read `path` and return the relocated load module.

    For flat images (no MZ header) the caller can supply entry/stack (absolute
    seg, off); they default to the game86 values below.
    """
    path = Path(path)
    raw = path.read_bytes()
    if raw[:2] in (b"MZ", b"ZM"):
        return _parse_mz(raw, load_seg)
    layout = _read_layout(path)
    if layout:
        load_seg = layout.get("load_segment", load_seg)
    cs, ip = flat_entry or layout.get("entry") or (load_seg, 0x0010)
    ss, sp = flat_stack or layout.get("stack") or ((load_seg + 0x2CA6) & 0xFFFF, 0x1112)
    return LoadedImage(load_seg=load_seg, data=raw,
                       min_extra_paras=layout.get("min_alloc_paras", 0),
                       entry_cs=cs, entry_ip=ip, stack_ss=ss, stack_sp=sp)


def _num(v) -> int:
    return int(v, 0) if isinstance(v, str) else int(v)


def _read_layout(path: Path) -> dict:
    """Metadata written by tools/extract next to <name>.unpacked as <name>.layout.json:
    load_segment, min_alloc_paras, relocated entry cs/ip and ss/sp.  Key nesting is
    read tolerantly; missing fields fall back to the defaults."""
    meta_path = path.with_suffix(".layout.json")
    if not meta_path.exists():
        return {}
    meta = json.loads(meta_path.read_text())
    out: dict = {}
    if "load_segment" in meta:
        out["load_segment"] = _num(meta["load_segment"])
    if "min_alloc_paras" in meta:
        out["min_alloc_paras"] = _num(meta["min_alloc_paras"])

    def pair(name: str, a: str, b: str):
        node = meta.get(name)
        if isinstance(node, dict):
            node = node.get("relocated", node)
            if a in node and b in node:
                return _num(node[a]), _num(node[b])
        flat_a, flat_b = f"{name}_{a}_relocated", f"{name}_{b}_relocated"
        if flat_a in meta and flat_b in meta:
            return _num(meta[flat_a]), _num(meta[flat_b])
        return None

    for key, a, b in (("entry", "cs", "ip"), ("stack", "ss", "sp")):
        p = pair(key, a, b)
        if p:
            out[key] = p
    return out
