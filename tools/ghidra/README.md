# tools/ghidra — headless Ghidra scripts (workstream W8)

All scripts run against the **headless** project `ghidra/headless/LlamatronCLI.gpr` through
`run_headless.sh`. The GUI project `ghidra/Llamatron.gpr` belongs to the user; never open or
modify it from automation (the user can run `ApplySymbols.java` there by hand).

```sh
tools/ghidra/run_headless.sh <program|all> <Script.java> [args...]
```
`program` is the project file name (`game86.exe`, `fs.exe`, `llama.exe`, `install.exe`); `all`
processes every program. `GHIDRA_HOME` defaults to the newest Homebrew Ghidra. Each run saves the
program back into the headless project, so stop any other headless run first (Ghidra locks it).

| Script | Args | Effect |
|---|---|---|
| `ApplySymbols.java` | `re/symbols.csv [dry] [types]` | names functions/labels, sets plate/EOL/pre comments, applies `dtype` with `types`; idempotent |
| `Disasm.java` | `<targets.txt>` | disassembles entry points / word tables (`table seg:off count [cs]`) and creates functions |
| `DumpTable.java` | `<seg:off> <count> [cs]` | prints a word table, optionally resolving `cs:<word>` targets |
| `Export.java` | `re/listings` | regenerates `<program>.lst` with symbolic operands and comments |
| `ExportFunctions.java` | `re/functions` | `<program>.functions.csv` (entry, name, size, instructions, callers, callees) |

Helpers (Python 3, stdlib only):
- `check_symbols.py re/symbols.csv` — validates columns, address syntax, naming rules and duplicates.
- `gen_funcmap.py` — (re)seeds `docs/funcmap.csv` from `re/functions/*.functions.csv` and
  `re/symbols.csv`, keeping status/owner/notes of rows that already exist.

## re/symbols.csv
Local only (`re/` is gitignored), single source of truth for names:
`program,address,name,type,comment[,dtype]` — `type` is `func`, `data` or `label`;
`dtype` is one of `byte word dword ptr16 farptr char[N] byte[N] word[N] dword[N]`.
Naming (docs/PLAN.md §4): data `g_<meaning>`, code `<subsystem>_<verb>`, tables `tbl_<meaning>`,
unknowns `unk_<seg>_<off>`. Comments stay descriptive (no copied strings or table contents).

Typical pass after editing the CSV:
```sh
python3 tools/ghidra/check_symbols.py re/symbols.csv
tools/ghidra/run_headless.sh all ApplySymbols.java re/symbols.csv types
tools/ghidra/run_headless.sh all Export.java re/listings
tools/ghidra/run_headless.sh all ExportFunctions.java re/functions
python3 tools/ghidra/gen_funcmap.py
```
