# strict-error-analyser

> **Created by [setmetatable24](https://github.com/setmetatable24)**  
> GitHub: [https://github.com/setmetatable24?tab=repositories](https://github.com/setmetatable24?tab=repositories)

**strict-error-analyser** is a high-performance modular C17 CLI & Antigravity/Studio MCP skill by **setmetatable24** that reproduces Roblox Studio's **Script Analysis** diagnostics (including the Luau New Type Solver, `LuauSolverV2=true`) from the terminal using [`luau-lsp analyze`](https://github.com/JohnnyMorganz/luau-lsp), dynamic Rojo `sourcemap.json` generation, and official Roblox `globalTypes.d.luau`.

## Highlights

- **Pure C17 Modular Architecture:** Native execution in milliseconds with zero dependencies. Split into clean units (`common`, `platform`, `json`, `workspace`, `analyzer`, `main`).
- **Complete Studio Parity:** Runs `luau-lsp analyze` with `--platform=roblox`, `--definitions:@roblox=globalTypes.d.luau`, `--flag:LuauSolverV2=true`, and `--flag:LuauNewTypePathErrorMessages=true`.
- **Rojo Sourcemap & Workspace Generation:** Emulates Rojo to accurately map `ReplicatedStorage`, `ServerScriptService`, `StarterPlayerScripts`, and nested ModuleScripts for cross-file Luau type resolution.
- **Chunked DataModel Exporter:** Exports places of any size from Roblox Studio via MCP `execute_luau` in 90 KB safe chunks with server-side caching (`shared.__RBX_SA_CACHE`).
- **Path Mapping:** Converts temporary workspace paths back to real Studio DataModel hierarchies (`game.ServerScriptService.Modules.ProfileService`).
- **Strict Rule Enforcer:** Optional `--check-rules` validator to enforce `--!strict` compliance on line 1 and ban Lua comments across production code.
- **Cross-Platform:** Builds with MSVC, GCC, or Clang via `build.bat`, `build.sh`, or `CMakeLists.txt`. Includes a Python 3 fallback (`run_analysis.py`).

## Directory Structure

```
strict-error-analyser/
├── include/
│   ├── common.h         # Dynamic buffers, safe allocation, string lists
│   ├── platform.h       # Filesystem, process execution, downloaders
│   ├── json.h           # AST node structures, recursive JSON parser
│   ├── workspace.h      # Sourcemap & DataModel directory tree builder
│   └── analyzer.h       # luau-lsp runner, GNU output & rule parser
├── src/
│   ├── common.c
│   ├── platform.c
│   ├── json.c
│   ├── workspace.c
│   ├── analyzer.c
│   └── main.c           # CLI entry point & orchestrator
├── scripts/
│   ├── export_datamodel.luau  # Studio DataModel chunked exporter
│   └── run_analysis.py        # Alternative Python 3 runner
├── resources/
│   └── globalTypes.d.luau     # Auto-downloaded Roblox type definitions
├── build.bat            # Windows MSVC/GCC/Clang build script
├── build.sh             # Linux/macOS build script
├── CMakeLists.txt       # Cross-platform CMake configuration
├── SKILL.md             # Antigravity agent skill specification
└── README.md
```

## Prerequisites

- **[Rokit](https://github.com/rojo-rbx/rokit)** (or `luau-lsp` directly on `PATH` / `LUAU_LSP_PATH`)
- A C17 compiler (`cl.exe` MSVC, `gcc`, or `clang`) or Python 3.10+

### Installing luau-lsp via Rokit

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

## Building

- **Windows:**
  ```cmd
  build.bat
  ```
- **macOS / Linux:**
  ```bash
  chmod +x build.sh && ./build.sh
  ```
- **CMake:**
  ```bash
  cmake -B build -S .
  cmake --build build --config Release
  ```

## Setup & Environment Verification

Run `--setup` to verify `luau-lsp` and download `resources/globalTypes.d.luau` automatically:

```bash
bin/strict-error-analyser --setup
```

## CLI Usage

```bash
strict-error-analyser [options]

options:
  -h, --help                     show this help message and exit
  --input <path...>              one or more exported chunk/JSON paths, directory, or '-' for stdin
  --setup                        verify or auto-install luau-lsp (via Rokit) and download globalTypes.d.luau
  --mode <new|old|both>          Luau type solver mode (default: new)
  --target <substr>              filter diagnostics to matching script paths (can be repeated)
  --errors-only                  only report type/syntax/rule errors, hide lint warnings
  --check-rules                  check '--!strict' on line 1 and no '--' comments
  --skip-rules-for <substr>      skip rule checks for matching scripts (can be repeated)
  --json                         emit structured JSON output
  --keep-temp                    preserve generated workspace folder for inspection
  --luau-lsp <path>              path or command name for luau-lsp (auto-detected if omitted)
  --global-types <path>          path to Roblox globalTypes.d.luau
```

## Installation into Antigravity

Clone into your global Antigravity skills directory (or `.agents/skills/` within a project):

```bash
# Global skills directory
git clone https://github.com/setmetatable24/strict-error-analyser.git ~/.gemini/config/skills/strict-error-analyser

# Or local workspace
git clone https://github.com/setmetatable24/strict-error-analyser.git .agents/skills/strict-error-analyser
```

## Author

Created by **setmetatable24**  
GitHub: [https://github.com/setmetatable24](https://github.com/setmetatable24?tab=repositories)
