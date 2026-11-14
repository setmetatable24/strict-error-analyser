# strict-error-analyser

> Created by **[setmetatable24](https://github.com/setmetatable24)**

**strict-error-analyser** is a modular **C17** CLI and Antigravity / Roblox Studio MCP skill that reproduces Roblox Studio's **Script Analysis** diagnostics (including the Luau New Type Solver) directly from the terminal.

## Why this exists

Roblox Studio's MCP server lets an AI agent read and edit scripts, **but exposes no API for the Script Analysis widget**. Without it, an agent has to guess whether a refactor introduced a `TypeError` or ask you to look at Studio manually.

This tool closes that loop: it extracts the live `DataModel` from Studio, reconstructs a Rojo-compatible workspace with a full `sourcemap.json`, runs `luau-lsp analyze` with Roblox's official `globalTypes.d.luau` and `LuauSolverV2=true`, and maps every diagnostic back to its exact `game.<InstancePath>` location.

---

## How it works

1. **Chunked Studio Export (`scripts/export_datamodel.luau`)**
   - Walks the `Edit` DataModel via `execute_luau`, capturing instance hierarchy and `LuaSourceContainer.Source`.
   - Serializes the snapshot to JSON and splits it into 90,000-byte UTF-8-safe chunks (`RBX_SA_CHUNK i/N`) cached in `shared.__RBX_SA_CACHE` so large places bypass MCP output truncation limits.
2. **Workspace & Sourcemap Synthesis (`src/json.c`, `src/workspace.c`)**
   - Reassembles chunked inputs (or reads from `stdin` / JSON files) using a zero-dependency recursive-descent JSON parser.
   - Materializes `.server.luau`, `.client.luau`, `.luau`, and `init.*.luau` files on disk, generates `sourcemap.json` + `.luaurc`, and builds a bidirectional disk-to-DataModel path table.
3. **Static Analysis & Rule Verification (`src/analyzer.c`)**
   - Executes `luau-lsp analyze --platform=roblox --formatter=gnu` with `--flag:LuauSolverV2=true` and `--flag:LuauNewTypePathErrorMessages=true`.
   - Resolves cross-script `require(...)` chains across `ReplicatedStorage`, `ServerScriptService`, and `StarterPlayerScripts`.
   - Optionally runs `--check-rules` to enforce `--!strict` on line 1 and flag `--` comments.

---

## Codebase layout

```text
strict-error-analyser/
├── include/
│   ├── common.h               # Safe allocators, dynamic byte buffer, string list
│   ├── platform.h             # Win32/POSIX FS, subprocess capture, URLDownloadToFile/curl
│   ├── json.h                 # InstanceNode AST, chunk reassembly, JSON parser
│   ├── workspace.h            # Rojo filesystem & sourcemap.json generator
│   └── analyzer.h             # luau-lsp runner, GNU output parser, strict rule checker
├── src/
│   ├── common.c
│   ├── platform.c
│   ├── json.c
│   ├── workspace.c
│   ├── analyzer.c
│   └── main.c                 # CLI entry point
├── scripts/
│   ├── export_datamodel.luau  # Studio MCP DataModel chunked exporter
│   └── run_analysis.py        # Fallback Python 3 runner
├── build.bat                  # Windows build (MSVC / GCC / Clang)
├── build.sh                   # Linux / macOS build
├── CMakeLists.txt             # C17 CMake target
├── SKILL.md                   # Antigravity agent skill definition
└── README.md
```

---

## Prerequisites

- **[Rokit](https://github.com/rojo-rbx/rokit)** (or `luau-lsp` already in `PATH` / `LUAU_LSP_PATH`)
- A **C17** compiler (`cl.exe` MSVC, `gcc`, or `clang`) — or **Python 3.10+** if using `scripts/run_analysis.py`

Install `luau-lsp` globally via Rokit:

```powershell
# Windows (PowerShell)
irm https://raw.githubusercontent.com/rojo-rbx/rokit/main/scripts/install.ps1 | iex
rokit trust JohnnyMorganz/luau-lsp
rokit add --global JohnnyMorganz/luau-lsp
```

```bash
# macOS / Linux
curl -sSf https://raw.githubusercontent.com/rojo-rbx/rokit/main/scripts/install.sh | sh
rokit trust JohnnyMorganz/luau-lsp
rokit add --global JohnnyMorganz/luau-lsp
```

---

## Building

**Windows (`build.bat` auto-detects MSVC, GCC, or Clang):**
```cmd
build.bat
```

**Linux / macOS:**
```bash
chmod +x build.sh && ./build.sh
```

**CMake:**
```bash
cmake -B build -S .
cmake --build build --config Release
```

---

## Setup & Usage

Run `--setup` once to verify `luau-lsp` (or auto-install it via Rokit) and download `resources/globalTypes.d.luau`:

```bash
bin/strict-error-analyser --setup
```

Analyze exported chunk files:

```bash
bin/strict-error-analyser --input chunk1.txt chunk2.txt --mode new --errors-only
```

### CLI Options

| Flag | Description |
| --- | --- |
| `--input <path...>` | Exported chunk/JSON files, or `-` for stdin |
| `--setup` | Verify/install `luau-lsp` via Rokit and fetch `globalTypes.d.luau` |
| `--mode <new\|old\|both>` | Luau type solver mode (default: `new`) |
| `--target <substr>` | Filter output to matching `game.<Path>` scripts |
| `--errors-only` | Show only `TypeError`, `SyntaxError`, and rule violations |
| `--check-rules` | Enforce `--!strict` on line 1 and ban `--` comments |
| `--skip-rules-for <substr>` | Exclude matching paths (e.g., vendor modules) from `--check-rules` |
| `--json` | Emit structured JSON diagnostics |
| `--keep-temp` | Keep the generated temporary workspace for debugging |
| `--luau-lsp <path>` | Override `luau-lsp` binary path |
| `--global-types <path>` | Override `globalTypes.d.luau` path |

---

## Installing as an Antigravity Skill

Clone directly into your global skills directory or a project's `.agents/skills/`:

```bash
# Global Antigravity skills
git clone https://github.com/setmetatable24/strict-error-analyser.git ~/.gemini/config/skills/strict-error-analyser

# Project-local skill
git clone https://github.com/setmetatable24/strict-error-analyser.git .agents/skills/strict-error-analyser
```

---

## License

MIT License — see [LICENSE](./LICENSE).  
Author: **[setmetatable24](https://github.com/setmetatable24?tab=repositories)**
