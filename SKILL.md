---
name: strict-error-analyser
description: >-
  Static Luau typechecking and Studio Script Analysis diagnostics (by setmetatable24) using luau-lsp analyze, Rojo sourcemap generation, and Roblox globalTypes.d.luau. Use whenever checking for TypeErrors, SyntaxErrors, --!strict compliance, or verifying script edits in an open Roblox Studio place without manual user inspection.
---

# strict-error-analyser

> **Created by [setmetatable24](https://github.com/setmetatable24)**  
> GitHub: [https://github.com/setmetatable24](https://github.com/setmetatable24?tab=repositories)

Static Luau typechecking skill and native C17 high-performance engine (`luau-lsp analyze` via Roblox Studio MCP) that reproduces Roblox Studio's **Script Analysis** diagnostics (including Luau New Type Solver, `LuauSolverV2=true`) so an AI agent can detect and verify every `TypeError`, `SyntaxError`, and lint across the entire `DataModel` automatically.

## Project Architecture

- **CLI Engine:** Modular **C17** (`include/` + `src/`) built with zero runtime dependencies.
  - `include/common.h` & `src/common.c`: Safe dynamic memory wrappers, dynamic string buffer, string list.
  - `include/platform.h` & `src/platform.c`: Cross-platform filesystem operations, URL downloader, executable discovery, process execution.
  - `include/json.h` & `src/json.c`: AST structures, streaming chunk reassembly, recursive-descent JSON parser.
  - `include/workspace.h` & `src/workspace.c`: DataModel filesystem exporter, Rojo `sourcemap.json` writer, `.luaurc` generator, path bidirectional mapping.
  - `include/analyzer.h` & `src/analyzer.c`: `luau-lsp` invocation, GNU diagnostics parser, strict `--!strict` & comment rule checking.
  - `src/main.c`: Clean entry point coordinating options, setup, workspace, and diagnostics reporting.
- **Build Scripts:** `build.bat` (MSVC / GCC / Clang), `build.sh` (Linux / macOS), `CMakeLists.txt`.
- **Studio Exporter:** [export_datamodel.luau](./scripts/export_datamodel.luau) — 90 KB chunked DataModel exporter with `shared.__RBX_SA_CACHE` caching.
- **Python Alternative:** [run_analysis.py](./scripts/run_analysis.py) — zero-dependency Python 3 script.
- **Roblox API Definitions:** [globalTypes.d.luau](./resources/globalTypes.d.luau) (auto-downloaded on `--setup`).

## Step 0: One-Time Prerequisite Setup (Cross-Platform)

Before the first run on a machine, compile the binary (or run `run_analysis.py`) and verify `luau-lsp` and `globalTypes.d.luau` using `--setup`:

- **Windows:**
  ```cmd
  build.bat
  bin\strict-error-analyser.exe --setup
  ```
- **macOS / Linux:**
  ```bash
  chmod +x build.sh && ./build.sh
  ./bin/strict-error-analyser --setup
  ```

What `--setup` does automatically:
1. Auto-detects `luau-lsp` via `LUAU_LSP_PATH`, `PATH`, `~/.rokit/bin`, `~/.aftman/bin`, `~/.foreman/bin`, or `~/.cargo/bin`.
2. If `luau-lsp` is missing and `rokit` is installed, runs `rokit trust JohnnyMorganz/luau-lsp` and `rokit add --global JohnnyMorganz/luau-lsp`.
3. Downloads `globalTypes.d.luau` into `resources/globalTypes.d.luau` if not already present.

If neither `luau-lsp` nor `rokit` is installed yet, install **Rokit** (https://github.com/rojo-rbx/rokit) first:

- **Windows (PowerShell):**
  ```powershell
  irm https://raw.githubusercontent.com/rojo-rbx/rokit/main/scripts/install.ps1 | iex
  rokit trust JohnnyMorganz/luau-lsp
  rokit add --global JohnnyMorganz/luau-lsp
  ```
- **macOS / Linux:**
  ```bash
  curl -sSf https://raw.githubusercontent.com/rojo-rbx/rokit/main/scripts/install.sh | sh
  rokit trust JohnnyMorganz/luau-lsp
  rokit add --global JohnnyMorganz/luau-lsp
  ```

## Workflow

### Step 1: Export Chunk 1 from Roblox Studio (`Edit` DataModel)

Read [export_datamodel.luau](./scripts/export_datamodel.luau) and execute its Luau code in Roblox Studio via `execute_luau` (`datamodel_type = "Edit"`).

- Each chunk starts with header line `RBX_SA_CHUNK <index>/<total> <totalBytes>`.
- If the MCP host saves large tool outputs to a file on disk, read **line 1 only** (`StartLine = 1, EndLine = 1`) to obtain `<index>/<total>`.

### Step 2: Fetch Remaining Chunks in Parallel (when `<total> > 1`)

If `<total> > 1`, invoke `execute_luau` (`datamodel_type = "Edit"`) in parallel for `CHUNK_INDEX = 2` through `<total>` (changing `local CHUNK_INDEX = <k>`). Chunk 1 caches the serialized snapshot in `shared.__RBX_SA_CACHE` so remaining chunks return in milliseconds. Save chunks to files.

### Step 3: Run Static Typechecking

Run the C17 binary (or Python runner) on all chunk files:

```bash
bin/strict-error-analyser --input <chunk1> <chunk2> ... --mode new
```

Common options:
- `--mode new` (default): Runs Luau New Type Solver (`--flag:LuauSolverV2=true --flag:LuauNewTypePathErrorMessages=true`), matching Studio Script Analysis.
- `--mode old` / `--mode both`: Checks the classic solver or compares both solvers.
- `--errors-only`: Prints only `TypeError` and `SyntaxError` (omits lints like `LocalUnused`).
- `--target <substr>`: Filters output to matching `game.<InstancePath>` scripts while still typechecking all cross-module `require` dependencies.
- `--check-rules`: Verifies `--!strict` on line 1 and no `--` comments on lines 2+ (`--skip-rules-for <name>` excludes legacy/vendor modules).
- `--json`: Outputs machine-readable JSON diagnostics.

### Step 4: Fix & Re-Verify

Fix any reported errors in Studio using `multi_edit` (`datamodel_type = "Edit"`), then repeat Steps 1–3 until `Errors: 0`.

---

## Author

- **setmetatable24** — [https://github.com/setmetatable24](https://github.com/setmetatable24?tab=repositories)
