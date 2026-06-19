#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

FALLBACK_WASM_OBJDUMP = r"C:\tmp\path\wabt-1.0.41\bin\wasm-objdump.exe"


def find_wasm_objdump():
    if shutil.which("wasm-objdump"):
        return "wasm-objdump"
    if os.path.isfile(FALLBACK_WASM_OBJDUMP):
        return FALLBACK_WASM_OBJDUMP
    print("Error: wasm-objdump not found in PATH and fallback path does not exist:")
    print(f"  {FALLBACK_WASM_OBJDUMP}")
    sys.exit(1)


def find_newest_wasm(build_dir: Path) -> Path:
    wasm_files = list(build_dir.rglob("*.wasm"))
    if not wasm_files:
        print(f"Error: No .wasm files found under {build_dir}")
        sys.exit(1)
    return max(wasm_files, key=lambda p: p.stat().st_mtime)


def parse_sections(output: str) -> list[tuple[str, int, int | None]]:
    sections = []
    in_sections = False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("Sections:"):
            in_sections = True
            continue
        if not in_sections or not stripped:
            continue
        m_size = re.search(r'\(size=0x([0-9a-fA-F]+)\)', stripped)
        m_count = re.search(r'count:\s*(\d+)', stripped)
        parts = stripped.split()
        if not parts or not m_size:
            continue
        name = parts[0]
        size = int(m_size.group(1), 16)
        count = int(m_count.group(1)) if m_count else None
        sections.append((name, size, count))
    return sections


def human_size(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{int(n)} B"
        n /= 1024


def bar(fraction: float, width: int = 28) -> str:
    filled = round(fraction * width)
    return "#" * filled + "-" * (width - filled)


def library_from_path(obj_path: Path, build_root: Path) -> str:
    """Extract a human-readable library name from a CMake object file path.
    Looks for the CMakeFiles/<libname>.dir pattern."""
    try:
        rel = obj_path.relative_to(build_root)
    except ValueError:
        return obj_path.parts[-1]
    parts = rel.parts
    for i, part in enumerate(parts):
        if part == "CMakeFiles" and i + 1 < len(parts):
            lib = parts[i + 1]
            if lib.endswith(".dir"):
                lib = lib[:-4]
            # strip autogen suffix Qt appends
            if lib.endswith("_autogen"):
                lib = lib[:-8]
            return lib
    return parts[0]


def analyze_objects(build_root: Path, wasm_objdump: str) -> dict[str, dict[str, int]]:
    """Run wasm-objdump -h on every .o file and accumulate Code/Data sizes per library."""
    obj_files = list(build_root.rglob("*.o"))
    totals: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    print(f"Scanning {len(obj_files)} object files...", flush=True)

    for obj in obj_files:
        result = subprocess.run(
            [wasm_objdump, str(obj), "-h"],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            continue
        sections = parse_sections(result.stdout)
        lib = library_from_path(obj, build_root)
        for sec_name, size, _ in sections:
            totals[lib][sec_name] += size

    return totals


def print_section_table(title: str, rows: list[tuple[str, int]], total: int, section_label: str = "Library"):
    print(f"\n{title}")
    name_w = max((len(r[0]) for r in rows), default=10)
    name_w = max(name_w, len(section_label))
    size_w = max((len(human_size(r[1])) for r in rows), default=6)
    size_w = max(size_w, 6)
    header = f"  {section_label:<{name_w}}  {'Size':>{size_w}}  {'%':>5}  {'Bar':28}"
    print(header)
    print("-" * len(header))
    for name, size in sorted(rows, key=lambda x: x[1], reverse=True):
        pct = size / total * 100 if total else 0
        b = bar(size / total if total else 0)
        print(f"  {name:<{name_w}}  {human_size(size):>{size_w}}  {pct:>4.1f}%  {b}")
    print("-" * len(header))
    print(f"  {'Total':<{name_w}}  {human_size(total):>{size_w}}")


def main():
    project_root = Path(__file__).resolve().parent.parent.parent
    build_dir = project_root / "build"

    deep = "--nodeep" not in sys.argv

    wasm_objdump = find_wasm_objdump()
    wasm_file = find_newest_wasm(build_dir)
    # The wasm-release build tree sits two levels above the .wasm file
    # (build/wasm-release/apps/webgpu_app/webgpu_app.wasm)
    build_root = wasm_file.parent.parent.parent  # build/wasm-release

    result = subprocess.run(
        [wasm_objdump, str(wasm_file), "-h"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("Error running wasm-objdump:")
        print(result.stderr)
        sys.exit(1)

    sections = parse_sections(result.stdout)
    if not sections:
        print("Could not parse section output.")
        print(result.stdout)
        sys.exit(1)

    total_sections = sum(s for _, s, _ in sections)
    file_size = wasm_file.stat().st_size

    print(f"\nWASM Binary Analysis")
    print(f"File  : {wasm_file}")
    print(f"Size  : {human_size(file_size)}")
    print(f"Build : {build_root}")

    # --- Section overview ---
    name_w = max(len(s[0]) for s in sections)
    size_w = max(len(human_size(s[1])) for s in sections)
    header = f"\n  {'Section':<{name_w}}  {'Size':>{size_w}}  {'%':>5}  {'Bar':28}  Count"
    print(header)
    print("-" * len(header))
    for name, size, count in sorted(sections, key=lambda x: x[1], reverse=True):
        pct = size / total_sections * 100
        b = bar(size / total_sections)
        count_str = str(count) if count is not None else ""
        print(f"  {name:<{name_w}}  {human_size(size):>{size_w}}  {pct:>4.1f}%  {b}  {count_str}")
    print("-" * len(header))
    print(f"  {'Total (sections)':<{name_w}}  {human_size(total_sections):>{size_w}}")

    if not deep:
        print(f"\nTip: pass --nodeep to skip the per-library breakdown")
        print()
        return

    # --- Per-library breakdown from object files ---
    lib_data = analyze_objects(build_root, wasm_objdump)

    code_rows = [(lib, secs.get("Code", 0)) for lib, secs in lib_data.items() if secs.get("Code", 0) > 0]
    data_rows = [(lib, secs.get("Data", 0)) for lib, secs in lib_data.items() if secs.get("Data", 0) > 0]

    total_code = sum(s for _, s in code_rows)
    total_data = sum(s for _, s in data_rows)

    print_section_table("Code section -- by library", code_rows, total_code)
    print_section_table("Data section -- by library", data_rows, total_data)
    print()


if __name__ == "__main__":
    main()
