#!/usr/bin/env python3
"""strict-error-analyser (by setmetatable24) - Export-to-luau-lsp bridge for Roblox Studio Script Analysis via MCP.

Author: setmetatable24 (https://github.com/setmetatable24)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path
from typing import Any

GLOBAL_TYPES_URL = "https://raw.githubusercontent.com/JohnnyMorganz/luau-lsp/main/scripts/globalTypes.d.luau"
DEFAULT_GLOBAL_TYPES = Path(__file__).resolve().parent.parent / "resources" / "globalTypes.d.luau"

INVALID_FS_CHARS = re.compile(r'[<>:"/\\|?*\x00-\x1f]')
DM_TAG_SUFFIX_RE = re.compile(r"\s+\[([^\]]+)\]$")
GNU_LINE_RE = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+)\.(?P<col>\d+)-(?P<end_line>\d+)\.(?P<end_col>\d+):\s*(?P<code>[A-Za-z0-9_]+):\s*(?P<msg>.*)$"
)
CHUNK_HEADER_RE = re.compile(
    r"^RBX_SA_CHUNK\s+(?P<idx>\d+)/(?P<total>\d+)\s+(?P<length>\d+)\r?\n(?P<body>.*)$",
    re.DOTALL,
)

SCRIPT_EXT_BY_CLASS = {
    "Script": ".server.luau",
    "LocalScript": ".client.luau",
    "ModuleScript": ".luau",
}

INIT_NAME_BY_CLASS = {
    "Script": "init.server.luau",
    "LocalScript": "init.client.luau",
    "ModuleScript": "init.luau",
}


def exe_name(base: str) -> str:
    if os.name == "nt" and not base.lower().endswith(".exe"):
        return f"{base}.exe"
    return base


def find_executable(name: str) -> Path | None:
    if name == "luau-lsp":
        env_override = os.environ.get("LUAU_LSP_PATH")
        if env_override:
            env_path = Path(env_override).expanduser()
            if env_path.is_file():
                return env_path

    which_hit = shutil.which(name)
    if which_hit:
        return Path(which_hit)

    filename = exe_name(name)
    home = Path.home()
    candidates = [
        home / ".rokit" / "bin" / filename,
        home / ".aftman" / "bin" / filename,
        home / ".foreman" / "bin" / filename,
        home / ".cargo" / "bin" / filename,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def download_global_types(dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = dest.with_suffix(".tmp")
    with urllib.request.urlopen(GLOBAL_TYPES_URL, timeout=30) as resp:
        data = resp.read()
    if len(data) < 1024:
        raise RuntimeError(f"Downloaded globalTypes.d.luau looks unexpectedly small ({len(data)} bytes).")
    tmp_path.write_bytes(data)
    tmp_path.replace(dest)


def ensure_dependencies(luau_lsp_arg: str | None, global_types: Path) -> tuple[Path, Path]:
    if not global_types.is_file():
        print(f"[setup] Downloading Roblox definitions to {global_types} ...", file=sys.stderr)
        download_global_types(global_types)

    luau_lsp: Path | None = None
    if luau_lsp_arg:
        explicit = Path(luau_lsp_arg).expanduser()
        if explicit.is_file():
            luau_lsp = explicit
        else:
            which_explicit = shutil.which(luau_lsp_arg)
            if which_explicit:
                luau_lsp = Path(which_explicit)
    else:
        luau_lsp = find_executable("luau-lsp")

    if luau_lsp is None:
        rokit = find_executable("rokit")
        if rokit is not None:
            print("[setup] luau-lsp not found; installing globally via rokit ...", file=sys.stderr)
            subprocess.run([str(rokit), "trust", "JohnnyMorganz/luau-lsp"], check=False)
            subprocess.run([str(rokit), "add", "--global", "JohnnyMorganz/luau-lsp"], check=False)
            luau_lsp = find_executable("luau-lsp")

    if luau_lsp is None or not luau_lsp.is_file():
        raise FileNotFoundError(
            "luau-lsp executable was not found.\n"
            "Install Rokit (https://github.com/rojo-rbx/rokit) and run:\n"
            "  rokit trust JohnnyMorganz/luau-lsp\n"
            "  rokit add --global JohnnyMorganz/luau-lsp\n"
            "Or pass --luau-lsp <path> / set LUAU_LSP_PATH."
        )

    return luau_lsp, global_types


def sanitize_segment(name: str) -> str:
    cleaned = INVALID_FS_CHARS.sub("_", name).strip()
    if not cleaned or cleaned in {".", ".."}:
        cleaned = "_unnamed"
    return cleaned


def norm_key(path_obj: Path) -> str:
    return os.path.normcase(os.path.realpath(str(path_obj)))


def load_datamodel_from_inputs(input_args: list[str]) -> dict[str, Any]:
    raw_texts: list[str] = []
    for item in input_args:
        if item == "-":
            raw_texts.append(sys.stdin.read())
        else:
            p = Path(item)
            if p.is_dir():
                for child in sorted(p.iterdir()):
                    if child.is_file():
                        raw_texts.append(child.read_text(encoding="utf-8", errors="replace"))
            else:
                raw_texts.append(p.read_text(encoding="utf-8", errors="replace"))

    if not raw_texts:
        raise ValueError("No input texts loaded.")

    chunks: dict[int, str] = {}
    expected_total: int | None = None
    expected_len: int | None = None

    for text in raw_texts:
        m = CHUNK_HEADER_RE.match(text)
        if m:
            idx = int(m.group("idx"))
            total = int(m.group("total"))
            length = int(m.group("length"))
            if expected_total is None:
                expected_total = total
                expected_len = length
            elif expected_total != total or expected_len != length:
                raise ValueError(
                    f"Mismatched chunk metadata: expected {expected_total}/{expected_len}, got {total}/{length}. Re-export all chunks together."
                )
            chunks[idx] = m.group("body")

    if expected_total is not None:
        missing = [i for i in range(1, expected_total + 1) if i not in chunks]
        if missing:
            raise ValueError(f"Missing chunk(s): {missing} out of {expected_total}.")
        combined = "".join(chunks[i] for i in range(1, expected_total + 1))
    elif len(raw_texts) == 1:
        combined = raw_texts[0]
    else:
        combined = "".join(raw_texts)

    stripped = combined.strip()
    try:
        parsed = json.loads(stripped)
        if isinstance(parsed, dict):
            return parsed
    except json.JSONDecodeError:
        pass

    start = combined.find("{")
    end = combined.rfind("}")
    if start == -1 or end == -1 or end <= start:
        raise ValueError("Combined input does not contain a valid JSON DataModel object.")
    parsed = json.loads(combined[start : end + 1])
    if not isinstance(parsed, dict):
        raise ValueError("Top-level JSON payload must be an object.")
    return parsed


def allocate_unique_dir(parent_dir: Path, desired: str, used_names: set[str]) -> Path:
    base = sanitize_segment(desired)
    candidate = base
    idx = 2
    while candidate.lower() in used_names:
        candidate = f"{base}__{idx}"
        idx += 1
    used_names.add(candidate.lower())
    target = parent_dir / candidate
    target.mkdir(parents=True, exist_ok=True)
    return target


def allocate_unique_file(parent_dir: Path, stem: str, suffix: str, used_names: set[str]) -> Path:
    base = sanitize_segment(stem)
    candidate = f"{base}{suffix}"
    idx = 2
    while candidate.lower() in used_names:
        candidate = f"{base}__{idx}{suffix}"
        idx += 1
    used_names.add(candidate.lower())
    return parent_dir / candidate


def build_workspace_tree(
    dm_root: dict[str, Any],
    work_dir: Path,
) -> tuple[dict[str, Any], dict[str, str], dict[str, str]]:
    src_root = work_dir / "src"
    src_root.mkdir(parents=True, exist_ok=True)

    file_to_dm_path: dict[str, str] = {}
    dm_path_to_source: dict[str, str] = {}

    def walk(
        node: dict[str, Any],
        current_dir: Path,
        dir_used_names: set[str],
        dm_prefix: str,
    ) -> dict[str, Any]:
        name = str(node.get("name", "Instance"))
        class_name = str(node.get("className", "Folder"))
        dm_path = f"{dm_prefix}.{name}" if dm_prefix else name

        children_raw = node.get("children") or []
        has_children = isinstance(children_raw, list) and len(children_raw) > 0
        is_script = class_name in SCRIPT_EXT_BY_CLASS

        sm_node: dict[str, Any] = {
            "name": name,
            "className": class_name,
        }

        child_dir = current_dir
        next_used: set[str] = set()

        if is_script:
            source_text = str(node.get("source") or "").replace("\r\n", "\n").replace("\r", "\n")
            if has_children:
                child_dir = allocate_unique_dir(current_dir, name, dir_used_names)
                script_file = child_dir / INIT_NAME_BY_CLASS[class_name]
                next_used.add(script_file.name.lower())
            else:
                script_file = allocate_unique_file(
                    current_dir,
                    name,
                    SCRIPT_EXT_BY_CLASS[class_name],
                    dir_used_names,
                )

            script_file.write_text(source_text, encoding="utf-8", newline="\n")
            rel_path = script_file.relative_to(work_dir).as_posix()
            sm_node["filePaths"] = [rel_path]
            file_to_dm_path[norm_key(script_file)] = dm_path
            file_to_dm_path[rel_path.lower()] = dm_path
            dm_path_to_source[dm_path] = source_text
        elif has_children:
            child_dir = allocate_unique_dir(current_dir, name, dir_used_names)

        if has_children:
            sm_children: list[dict[str, Any]] = []
            for child in children_raw:
                if isinstance(child, dict):
                    sm_children.append(walk(child, child_dir, next_used, dm_path))
            if sm_children:
                sm_node["children"] = sm_children

        return sm_node

    service_sm_children: list[dict[str, Any]] = []
    root_used: set[str] = set()
    for service in dm_root.get("children") or []:
        if isinstance(service, dict):
            service_sm_children.append(walk(service, src_root, root_used, "game"))

    sourcemap = {
        "name": "Place",
        "className": "DataModel",
        "children": service_sm_children,
    }
    return sourcemap, file_to_dm_path, dm_path_to_source


def resolve_dm_path(raw_file: str, work_dir: Path, file_to_dm_path: dict[str, str]) -> str:
    dm_fallback: str | None = None
    tag_match = DM_TAG_SUFFIX_RE.search(raw_file)
    clean_file = raw_file
    if tag_match:
        dm_fallback = tag_match.group(1).replace("/", ".")
        clean_file = raw_file[: tag_match.start()].strip()

    p = Path(clean_file)
    if not p.is_absolute():
        rel_key = clean_file.replace("\\", "/").lower()
        if rel_key in file_to_dm_path:
            return file_to_dm_path[rel_key]
        p = work_dir / p

    key = norm_key(p)
    if key in file_to_dm_path:
        return file_to_dm_path[key]
    if dm_fallback:
        return dm_fallback
    return clean_file


def parse_gnu_diagnostics(
    output_text: str,
    work_dir: Path,
    file_to_dm_path: dict[str, str],
) -> list[dict[str, Any]]:
    diagnostics: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None

    for raw_line in output_text.splitlines():
        line = raw_line.rstrip("\r\n")
        if line.startswith("[INFO]") or not line.strip():
            continue

        match = GNU_LINE_RE.match(line)
        if match:
            if current is not None:
                diagnostics.append(current)
            dm_path = resolve_dm_path(match.group("path"), work_dir, file_to_dm_path)
            current = {
                "script": dm_path,
                "line": int(match.group("line")),
                "col": int(match.group("col")),
                "endLine": int(match.group("end_line")),
                "endCol": int(match.group("end_col")),
                "code": match.group("code"),
                "message": match.group("msg").strip(),
            }
        elif current is not None:
            continuation = line.strip()
            if continuation:
                current["message"] = (current["message"] + "\n  " + continuation).strip()

    if current is not None:
        diagnostics.append(current)

    return diagnostics


def run_luau_lsp(
    luau_lsp: Path,
    global_types: Path,
    work_dir: Path,
    mode: str,
    file_to_dm_path: dict[str, str],
) -> tuple[int, list[dict[str, Any]], str]:
    sourcemap_path = work_dir / "sourcemap.json"
    luaurc_path = work_dir / ".luaurc"
    luaurc_path.write_text(json.dumps({"languageMode": "nonstrict"}, indent=2), encoding="utf-8")

    cmd = [
        str(luau_lsp),
        "analyze",
        f"--sourcemap={sourcemap_path}",
        f"--definitions:@roblox={global_types}",
        f"--base-luaurc={luaurc_path}",
        "--platform=roblox",
        "--formatter=gnu",
    ]
    if mode == "new":
        cmd.extend(["--flag:LuauSolverV2=true", "--flag:LuauNewTypePathErrorMessages=true"])
    cmd.append("src")

    proc = subprocess.run(
        cmd,
        cwd=str(work_dir),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    combined = "\n".join(part for part in (proc.stdout, proc.stderr) if part)
    diagnostics = parse_gnu_diagnostics(combined, work_dir, file_to_dm_path)
    return proc.returncode, diagnostics, combined


def check_custom_rules(
    dm_path_to_source: dict[str, str],
    skip_targets: list[str],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    for dm_path, src in sorted(dm_path_to_source.items()):
        if any(s.lower() in dm_path.lower() for s in skip_targets):
            continue
        lines = src.split("\n")
        if not lines or lines[0] != "--!strict":
            issues.append(
                {
                    "script": dm_path,
                    "line": 1,
                    "col": 1,
                    "code": "RuleMissingStrict",
                    "message": "Line 1 is not '--!strict'",
                }
            )
        for idx, text in enumerate(lines[1:], start=2):
            if "--" in text:
                issues.append(
                    {
                        "script": dm_path,
                        "line": idx,
                        "col": text.index("--") + 1,
                        "code": "RuleLuaComment",
                        "message": "Comment token '--' detected outside line 1 directive",
                    }
                )
    return issues


def is_error_diagnostic(diag: dict[str, Any]) -> bool:
    code = str(diag.get("code", ""))
    return code in {"TypeError", "SyntaxError", "ParseError", "StaticError", "RuleMissingStrict", "RuleLuaComment"}


def filter_diagnostics(
    diagnostics: list[dict[str, Any]],
    targets: list[str],
    errors_only: bool,
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for diag in diagnostics:
        if errors_only and not is_error_diagnostic(diag):
            continue
        if targets:
            script_name = str(diag.get("script", ""))
            if not any(t.lower() in script_name.lower() for t in targets):
                continue
        result.append(diag)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="strict-error-analyser",
        description="Analyze exported Roblox Studio DataModel using luau-lsp.",
        epilog="Created by setmetatable24 (https://github.com/setmetatable24)",
    )
    parser.add_argument(
        "--input",
        nargs="+",
        required=False,
        help="One or more exported chunk/JSON paths, directory, or '-' for stdin.",
    )
    parser.add_argument("--setup", action="store_true", help="Verify or auto-install luau-lsp (via Rokit) and download globalTypes.d.luau.")
    parser.add_argument("--mode", choices=["new", "old", "both"], default="new", help="Luau type solver mode.")
    parser.add_argument("--target", action="append", default=[], help="Substring filter for script Instance paths.")
    parser.add_argument("--errors-only", action="store_true", help="Only report type/syntax/rule errors, hide lint warnings.")
    parser.add_argument("--check-rules", action="store_true", help="Also check --!strict line 1 and no '--' comments.")
    parser.add_argument("--skip-rules-for", action="append", default=[], help="Skip --check-rules for matching script paths.")
    parser.add_argument("--json", action="store_true", help="Emit structured JSON output.")
    parser.add_argument("--keep-temp", action="store_true", help="Preserve generated workspace folder for inspection.")
    parser.add_argument("--luau-lsp", default=None, help="Path or command name for luau-lsp (auto-detected if omitted).")
    parser.add_argument("--global-types", default=str(DEFAULT_GLOBAL_TYPES), help="Path to Roblox globalTypes.d.luau.")
    args = parser.parse_args()

    try:
        luau_lsp, global_types = ensure_dependencies(args.luau_lsp, Path(args.global_types).expanduser())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    if args.setup:
        ver_proc = subprocess.run([str(luau_lsp), "--version"], capture_output=True, text=True)
        version = (ver_proc.stdout or ver_proc.stderr or "").strip()
        if args.json:
            print(json.dumps({"luauLsp": str(luau_lsp), "version": version, "globalTypes": str(global_types)}, indent=2))
        else:
            print(f"luau-lsp: {luau_lsp} ({version})")
            print(f"globalTypes: {global_types} ({global_types.stat().st_size} bytes)")
        if not args.input:
            return 0

    if not args.input:
        parser.error("--input is required unless running --setup")

    dm_root = load_datamodel_from_inputs(args.input)
    work_dir = Path(tempfile.mkdtemp(prefix="rbx_sa_"))

    try:
        sourcemap, file_to_dm_path, dm_path_to_source = build_workspace_tree(dm_root, work_dir)
        (work_dir / "sourcemap.json").write_text(
            json.dumps(sourcemap, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

        modes = ["new", "old"] if args.mode == "both" else [args.mode]
        all_results: dict[str, list[dict[str, Any]]] = {}
        exit_code = 0

        for m in modes:
            _, diags, _ = run_luau_lsp(luau_lsp, global_types, work_dir, m, file_to_dm_path)
            if args.check_rules:
                diags = diags + check_custom_rules(dm_path_to_source, args.skip_rules_for)
            filtered = filter_diagnostics(diags, args.target, args.errors_only)
            all_results[m] = filtered
            if any(is_error_diagnostic(d) for d in filtered):
                exit_code = 1

        if args.json:
            payload = {
                "scriptsAnalyzed": len(dm_path_to_source),
                "tempDir": str(work_dir) if args.keep_temp else None,
                "results": all_results,
            }
            print(json.dumps(payload, ensure_ascii=False, indent=2))
        else:
            print(f"Analyzed {len(dm_path_to_source)} Studio scripts (solver={args.mode}).")
            for m, diags in all_results.items():
                errors = [d for d in diags if is_error_diagnostic(d)]
                warnings = [d for d in diags if not is_error_diagnostic(d)]
                print(f"\n=== Solver: {m} | Errors: {len(errors)} | Lints/Warnings: {len(warnings)} ===")
                if not diags:
                    print("No diagnostics found.")
                for d in diags:
                    print(f"{d['script']}:{d['line']}:{d['col']} [{d['code']}] {d['message']}")
            if args.keep_temp:
                print(f"\nTemp workspace preserved at: {work_dir}")

        return exit_code
    finally:
        if not args.keep_temp:
            shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
