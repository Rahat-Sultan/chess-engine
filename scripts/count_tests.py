#!/usr/bin/env python3
"""
Test counter and reconciler for LightweightChessEngine.
Verifies test counts against tests/test_manifest.txt and live test binary execution.
Fails if manifest and execution disagree or if any binary's count shrinks.
"""

import sys
import re
import subprocess
from pathlib import Path

def parse_manifest(manifest_path: Path):
    if not manifest_path.is_file():
        print(f"Error: manifest file not found: {manifest_path}", file=sys.stderr)
        sys.exit(1)

    binary_counts = {}
    current_binary = None
    header_re = re.compile(r"^##\s+(\S+)\s+\((\d+)\s+tests?\)")
    item_re = re.compile(r"^-\s+(.+)$")
    
    with open(manifest_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            h_match = header_re.match(line)
            if h_match:
                current_binary = Path(h_match.group(1)).name
                expected_count = int(h_match.group(2))
                binary_counts[current_binary] = {"expected": expected_count, "items": []}
                continue
            if current_binary and item_re.match(line):
                binary_counts[current_binary]["items"].append(item_re.match(line).group(1))

    for bin_name, data in binary_counts.items():
        if len(data["items"]) != data["expected"]:
            print(f"Error: manifest section for {bin_name} has count {data['expected']} but lists {len(data['items'])} items", file=sys.stderr)
            sys.exit(1)

    return binary_counts

def run_test_target(bin_name: str, build_dir: Path, repo_root: Path):
    if bin_name == "test_api":
        api_py = repo_root / "tests" / "test_api.py"
        if not api_py.is_file():
            return False, 0, f"Test file not found: {api_py}"
        venv_py = repo_root / ".venv" / "bin" / "python"
        py_cmd = str(venv_py) if venv_py.is_file() else sys.executable
        res = subprocess.run([py_cmd, str(api_py)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    else:
        bin_path = build_dir / bin_name
        if not bin_path.is_file():
            return False, 0, f"Binary not found: {bin_path}"
        res = subprocess.run([str(bin_path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    if res.returncode != 0:
        return False, 0, f"Process exited with code {res.returncode}"

    # Match "=== Results: X passed, 0 failed ==="
    match = re.search(r"===\s*Results:\s*(\d+)\s*passed,\s*(\d+)\s*failed\s*===", res.stdout)
    if match:
        passed = int(match.group(1))
        failed = int(match.group(2))
        if failed > 0:
            return False, passed, f"{failed} test(s) failed"
        return True, passed, "OK"

    # Fallback to counting [ PASS ] lines
    pass_matches = re.findall(r"\[\s*PASS\s*\]", res.stdout)
    return True, len(pass_matches), "OK"

def main():
    repo_root = Path(__file__).resolve().parent.parent
    manifest_path = repo_root / "tests" / "test_manifest.txt"
    build_dir = repo_root / "build"

    manifest_data = parse_manifest(manifest_path)

    print(f"{'Binary':<26} {'Manifest':<10} {'Executed':<10} {'Status':<10}")
    print("-" * 58)

    total_manifest = 0
    total_executed = 0
    has_error = False

    for bin_name, data in manifest_data.items():
        expected = data["expected"]
        total_manifest += expected

        ok, executed, status_msg = run_test_target(bin_name, build_dir, repo_root)

        total_executed += executed
        status = "OK"

        if not ok:
            status = f"FAIL ({status_msg})"
            has_error = True
        elif executed != expected:
            if executed < expected:
                status = f"SHRUNK ({executed} < {expected})"
            else:
                status = f"MISMATCH ({executed} != {expected})"
            has_error = True

        print(f"{bin_name:<26} {expected:<10} {executed:<10} {status:<10}")

    print("-" * 58)
    total_status = "OK" if not has_error else "MISMATCH / FAILURE"
    print(f"{'Total':<26} {total_manifest:<10} {total_executed:<10} {total_status:<10}")

    if has_error:
        print("\nERROR: Discrepancy detected between test manifest and execution!", file=sys.stderr)
        sys.exit(1)

    print("\nAll test counts match the manifest exactly.")
    sys.exit(0)

if __name__ == "__main__":
    main()
