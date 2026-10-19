"""W2 high-score storage: game86 hiscore_save 1000:5e89 (create scores.yak, write the 0xA0
bytes of the table at DS:40A0, close) against the host's save path, the scores_store tool
(src/platform/testapp/scores_store.c: scores_parse_yak, scores_format_yak as the game turns its
table into bytes, plat_scores_save into LLAMATRON_PREF_DIR).

Same file name, same bytes, nothing else written. Plus the host's side of 1000:60aa: a saved
file is taken only if it is exactly 0xA0 bytes; a failed write is reported, never fatal (the
original ignores the DOS error and goes on).

Tables are random (8 ASCII digits + 8 name bytes per row). The user's own scores.yak, if
present, round-trips byte for byte; it is read at run time and never written into the repo.
"""

import os
import stat
import subprocess
from pathlib import Path

import pytest
from generators import case_rng, fuzz_cases
from unicorn import x86_const as X

from fs_oracle import original_dir

TABLE, TABLE_SIZE = 0x40A0, 0xA0
FILE_NAME = 0x0408                      # DS:0408 "scores.yak" (5e89 MOV DX,0x408)
SAVE = "1000:5e89"
HANDLE = 7
CARRY = 0x0001


@pytest.fixture(scope="module")
def store_tool() -> Path:
    tool = os.environ.get("LLAMATRON_SCORES_STORE")
    if not tool:
        testlib = os.environ.get("LLAMATRON_GAME_TESTLIB")
        if not testlib:
            pytest.skip("build not known (LLAMATRON_GAME_TESTLIB / LLAMATRON_SCORES_STORE unset)")
        tool = Path(testlib).parents[1] / "src/platform/scores_store"
    if not Path(tool).exists():
        pytest.skip(f"scores_store not built ({tool})")
    return Path(tool)


def random_table(rnd) -> bytes:
    rows = []
    for _ in range(10):
        digits = bytes(rnd.choice(b"0123456789") for _ in range(8))
        if rnd.random() < 0.2:
            digits = rnd.choice([b"00000000", b"99999999"])
        name = bytes(rnd.randrange(256) for _ in range(8))
        if rnd.random() < 0.5:
            name = bytes(rnd.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ .!") for _ in range(8))
        rows.append(digits + name)
    return b"".join(rows)


class DosWrite:
    """INT 21h for 5e89: create (3Ch), write (40h), close (3Eh), recorded."""

    def __init__(self, fail_create: bool = False):
        self.fail_create = fail_create
        self.calls: list[tuple] = []

    def __call__(self, m, _intno):
        r = m.get_regs()
        ah, ok, ax = r["ax"] >> 8, True, r["ax"]
        if ah == 0x3C:
            raw = m.read(r["ds"], r["dx"], 64)
            self.calls.append(("create", raw[:raw.index(0)].decode("ascii"), r["cx"]))
            ok, ax = (False, 5) if self.fail_create else (True, HANDLE)
        elif ah == 0x40:
            self.calls.append(("write", r["bx"], m.read(r["ds"], r["dx"], r["cx"])))
            ax = r["cx"]
        elif ah == 0x3E:
            self.calls.append(("close", r["bx"]))
        else:
            raise AssertionError(f"5e89 called INT 21h AH={ah:02x}h")
        m.uc.reg_write(X.UC_X86_REG_AX, ax & 0xFFFF)
        flags = m.uc.reg_read(X.UC_X86_REG_EFLAGS)
        m.uc.reg_write(X.UC_X86_REG_EFLAGS, flags & ~CARRY if ok else flags | CARRY)


def original_save(machine, table: bytes, fail_create: bool = False) -> list[tuple]:
    machine.reset()
    machine.write_dgroup(TABLE, table)
    dos = DosWrite(fail_create)
    machine.call(SAVE, interrupts={0x21: dos})
    return dos.calls


def host(tool: Path, prefs: Path, *args: str) -> str:
    out = subprocess.run([str(tool), *args], capture_output=True, text=True, timeout=30,
                         env=dict(os.environ, LLAMATRON_PREF_DIR=str(prefs)))
    assert out.returncode == 0, out.stderr
    return out.stdout.strip()


def test_save_matches_original(machine, store_tool, tmp_path):
    for case in fuzz_cases(20):
        table = random_table(case_rng("hiscore_save", case))
        calls = original_save(machine, table)
        assert [c[0] for c in calls] == ["create", "write", "close"], calls
        name, attributes = calls[0][1], calls[0][2]
        assert attributes == 0 and calls[1][1] == HANDLE and calls[2][1] == HANDLE
        written = calls[1][2]
        assert written == table and len(written) == TABLE_SIZE

        prefs = tmp_path / f"case{case}"
        prefs.mkdir()
        (prefs / "table.bin").write_bytes(table)
        assert host(store_tool, prefs, "save", str(prefs / "table.bin")) == "saved: yes"
        saved = [p.name for p in prefs.iterdir() if p.name != "table.bin"]
        assert saved == [name.lower()], f"case {case}: host wrote {saved}, the original {name}"
        assert (prefs / saved[0]).read_bytes() == written, f"case {case}: bytes differ"
        # ... and the next start reads them back (1000:60aa)
        assert host(store_tool, prefs, "load", str(prefs / "back.bin")) == "loaded: yes"
        assert (prefs / "back.bin").read_bytes() == table


def test_create_failure_is_not_fatal(machine, store_tool, tmp_path):
    # The original returns at once when the create fails (5e90 JC), writing nothing.
    calls = original_save(machine, random_table(case_rng("hiscore_save_fail", 0)), fail_create=True)
    assert [c[0] for c in calls] == ["create"]
    # The host reports the failed write and goes on.
    prefs = tmp_path / "readonly"
    prefs.mkdir()
    (tmp_path / "table.bin").write_bytes(random_table(case_rng("hiscore_save_fail", 1)))
    prefs.chmod(stat.S_IRUSR | stat.S_IXUSR)
    try:
        if os.access(prefs, os.W_OK):
            pytest.skip("cannot make a read-only directory here (running as root?)")
        assert host(store_tool, prefs, "save", str(tmp_path / "table.bin")) == "saved: no"
    finally:
        prefs.chmod(stat.S_IRWXU)


def test_load_takes_only_exact_size(store_tool, tmp_path):
    # 1000:60aa asks for 0xA1 bytes and keeps the table only if exactly 0xA0 came.
    table = random_table(case_rng("hiscore_load", 0))
    for size, want in ((TABLE_SIZE - 1, "no"), (TABLE_SIZE, "yes"), (TABLE_SIZE + 1, "no")):
        prefs = tmp_path / f"size{size}"
        prefs.mkdir()
        (prefs / "scores.yak").write_bytes((table + b"x")[:size])
        assert host(store_tool, prefs, "load", str(prefs / "back.bin")) == f"loaded: {want}"
    prefs = tmp_path / "none"
    prefs.mkdir()
    assert host(store_tool, prefs, "load", str(prefs / "back.bin")) == "loaded: no"


def test_users_scores_round_trip(store_tool, tmp_path):
    """The user's own scores.yak: parse then format gives the same bytes (the save path)."""
    path = original_dir() / "scores.yak"
    if not path.exists():
        pytest.skip("no scores.yak in the original folder")
    data = path.read_bytes()
    if len(data) != TABLE_SIZE or not all(48 <= data[i * 16 + d] <= 57 for i in range(10) for d in range(8)):
        pytest.skip("the user's scores.yak is not a table the game would load")
    (tmp_path / "table.bin").write_bytes(data)
    assert host(store_tool, tmp_path, "save", str(tmp_path / "table.bin")) == "saved: yes"
    assert (tmp_path / "scores.yak").read_bytes() == data
