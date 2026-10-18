"""Differential-test oracle for the Llamatron port (test-only, never linked into src/)."""

from .mzimage import LoadedImage, load_image
from .runner import CallResult, DiffRange, HarnessError, Machine, default_game86_path, linear

__all__ = ["CallResult", "DiffRange", "HarnessError", "LoadedImage", "Machine",
           "default_game86_path", "linear", "load_image"]
