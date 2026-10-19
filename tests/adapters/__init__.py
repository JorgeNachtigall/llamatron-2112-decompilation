"""Adapters between the port's state and the original's byte layout.

Only tests know original offsets (docs/PLAN.md §0.1). Python adapters here cover
the harness side; tests/adapters/dgroup_map.c will do the same for the C GameState.
"""
