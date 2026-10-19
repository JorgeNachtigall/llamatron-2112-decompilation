# Tests

- `oracle/test_*.py`: differential tests. Each runs original game86 routines through
  `tools/harness` and compares them with our behaviour. Today that is a Python
  reference; once the C functions exist in `src/`, it will be those functions.
- `adapters/`: the only place that knows original offsets. These modules convert
  between our state structs and the DGROUP byte layout (`rng.py` now;
  `dgroup_map.c` for the C `GameState` later).

Run:

    tools/harness/setup_venv.sh
    cmake -S tests -B build/tests && ctest --test-dir build/tests      # standalone
    # or, from the top-level build once it does add_subdirectory(tests):
    ctest --test-dir build -L oracle

Without a game image (`original/` not extracted), the oracle tests report *Skipped*.
