# Native power row classification

The former UI Category4/8 allowlist is removed. It was not a complete native spell-type check.
Castable Magic range1/2/4 plus the proven InventoryIndex mapping resolve a native spell snapshot.
Native types0 (Spell),2 (Power),3 (LesserPower) receive actual school/effect masks. Powers and lesser
powers both receive the Powers bit as well as the game's row school. Native effects use the existing
verified taxonomy; no spell-name exclusions or new category numeric guesses.

See sortingoverhaul/MagicRows.h and tests/magic_rows_tests.cpp. The first six native spell snapshots
from the recorded2026-10-24 capture are fixtures; their row schools are synthetic test inputs.
CommonLib enum values are compile-time asserted. A truly invalid range/index/snapshot/native kind
retains the old fail-open fallback and emits at most one warning per process for a castable row.
Eight-effect and600-spell read limits are unchanged.

This is a targeted correction, NOT proof that every live cause of SO-007 is solved. Owner retest:
School All +Fire should exclude the reported non-Fire powers; Fortify should retain Gates of Aetherius;
Effect All +Destruction should exclude it; Powers should retain it.
The18-choice UI, input ownership, typography, icons, settings and SearchLight ABI are unchanged.
