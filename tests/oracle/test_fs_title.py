"""W2 front screen: fs.exe's show_title 1000:01bf (lbm_load 0293, cmap_apply 043a with 04b0,
body_decode 2211 / byterun1_row 231f / src_wrap 2354 / skip_mask_row 2362, clear_load_buf
238b, planar_to_chunky 04cc / 0536, chunky_to_modex 23a2 / 239d, vga_set_320x400 23fd,
ega_show_planes 0229, ega_palette_set 04c1) run in unicorn on the user's original pictures
(tests/oracle/fs_oracle.py), against the port: the extractor's title files
(tools/extract/ilbm.c, title.c) and what title_load (src/engine/title.c) shows, through the
title_hash tool (FNV-1a of pixels + 8-bit palette).
"""

import os
import subprocess
from pathlib import Path

import pytest

from fs_oracle import fs_image_path, original_dir, run_show_title, verified_originals

PICTURES = {"NEWTIT.LBM", "NEWTIT2.LBM", "TRONEGA.LBM"}


def fnv1a(data: bytes, h: int = 2166136261) -> int:
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def dac6_to_8(v: int) -> int:
    return (v << 2 | v >> 4) & 0xFF


def ega_colour(v: int) -> tuple[int, int, int]:
    return (((v >> 2) & 1) * 0xAA + ((v >> 5) & 1) * 0x55,
            ((v >> 1) & 1) * 0xAA + ((v >> 4) & 1) * 0x55,
            (v & 1) * 0xAA + ((v >> 3) & 1) * 0x55)


def port_paths():
    """(generated data dir, title_hash executable) of the build under test."""
    game86, testlib = os.environ.get("LLAMATRON_GAME86_IMAGE"), os.environ.get("LLAMATRON_GAME_TESTLIB")
    if not game86 or not testlib:
        pytest.skip("generated data / build not known (LLAMATRON_GAME86_IMAGE, LLAMATRON_GAME_TESTLIB)")
    data = Path(game86).parents[1] / "data"
    tool = Path(os.environ.get("LLAMATRON_TITLE_HASH") or Path(testlib).parents[1] / "src/engine/title_hash")
    if not data.is_dir() or not tool.exists():
        pytest.skip(f"no {data} or {tool}")
    return data, tool


@pytest.fixture(scope="module")
def originals():
    image = fs_image_path()
    if image is None:
        pytest.skip("no fs.exe image (build the extractor)")
    files = verified_originals(original_dir())
    if not PICTURES <= files.keys():
        pytest.skip("original title pictures not found")
    return image, files


def first_diff(a: bytes, b: bytes, width: int) -> str:
    i = next(i for i in range(min(len(a), len(b))) if a[i] != b[i])
    return f"first difference at x={i % width} y={i // width}: fs.exe {a[i]}, port {b[i]}"


def title_hash(tool: Path, data: Path, ega: bool) -> str:
    args = [str(tool), "--data", str(data)] + (["--ega"] if ega else [])
    out = subprocess.run(args, capture_output=True, text=True, timeout=60)
    assert out.returncode == 0, out.stderr
    return out.stdout.split("hash=")[1].split()[0]


def test_vga_title(originals):
    data, tool = port_paths()
    image, files = originals
    run = run_show_title(image, files, vga=True)
    assert run.opened == ["NEWTIT.LBM", "NEWTIT2.LBM"]
    assert len(run.dac_calls) == 1 and run.dac_calls[0][0] == 0      # only NEWTIT's CMAP
    pixels, dac = run.pixels(), run.dac()

    port_pixels = (data / "title_vga.idx8").read_bytes()
    assert pixels == port_pixels, first_diff(pixels, port_pixels, 320)
    port_dac = (data / "title_vga.pal").read_bytes()
    assert bytes(v for c in dac for v in c) == port_dac

    palette = bytes(dac6_to_8(v) for c in dac for v in c)
    assert title_hash(tool, data, ega=False) == f"{fnv1a(palette, fnv1a(pixels)):08x}"


def test_ega_title(originals):
    data, tool = port_paths()
    image, files = originals
    run = run_show_title(image, files, vga=False)
    assert run.opened == ["TRONEGA.LBM"]
    assert run.dac_calls == []
    # cmap_apply sets the registers, ega_show_planes blanks them while copying, then sets them again
    assert len(run.ega_calls) == 3 and run.ega_calls[0] == run.ega_calls[2]
    pixels, registers = run.pixels(), run.ega_calls[-1]

    port_pixels = (data / "title_ega.idx4").read_bytes()
    assert pixels == port_pixels, first_diff(pixels, port_pixels, 640)
    assert registers == (data / "title_ega.pal").read_bytes()

    palette = bytes(v for i in range(16) for v in ega_colour(registers[i])) + bytes(3 * 240)
    assert title_hash(tool, data, ega=True) == f"{fnv1a(palette, fnv1a(pixels)):08x}"
