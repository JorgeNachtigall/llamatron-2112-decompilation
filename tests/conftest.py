"""pytest setup for the differential tests (test-only; see docs/PLAN.md §3.2)."""

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "harness"))
sys.path.insert(0, str(ROOT / "tests"))

from llamatron_harness import Machine, default_game86_path  # noqa: E402


@pytest.fixture(scope="session")
def game86_path() -> Path:
    try:
        return default_game86_path()
    except FileNotFoundError as exc:
        pytest.skip(str(exc))


@pytest.fixture
def machine(game86_path) -> Machine:
    """A freshly loaded game86 image (relocated at 1000, DS = DGROUP)."""
    return Machine.from_game86(game86_path)
