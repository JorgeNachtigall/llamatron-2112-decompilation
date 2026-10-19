"""ctypes access to the port's game logic (llamatron_game_testlib, test-only).

The library path comes from $LLAMATRON_GAME_TESTLIB, which ctest sets
(tests/cmake/TestLib.cmake).  Tests use the `cgame` fixture, which skips
when the library has not been built.
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path


def load_testlib() -> ctypes.CDLL | None:
    path = os.environ.get("LLAMATRON_GAME_TESTLIB")
    if not path or not Path(path).exists():
        return None
    return ctypes.CDLL(path)


def bind(lib: ctypes.CDLL, name: str, restype, *argtypes):
    """Declare a C function's signature once and return it."""
    fn = getattr(lib, name)
    fn.restype = restype
    fn.argtypes = list(argtypes)
    return fn
