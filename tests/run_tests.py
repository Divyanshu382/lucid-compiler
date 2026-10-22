#!/usr/bin/env python3
"""
Lucid test runner.

Every test is a .lucid file under tests/cases/ whose first three lines state
what the compiler must do:

    // expect-exit: 0                     process exit code (0 ok, 1 errors)
    // expect-output: 15 | B              printed lines, separated by '|'
    // expect-errors: semantic:TypeMismatch | syntax:UnexpectedToken
    // stdin: 3 4                         optional: what cin reads (\n allowed)

Use `expect-output: *` when the printed output should not be compared (for
example a program that prints until it is stopped).

The runner compiles each file with build/lucidc and compares all three.

    python3 tests/run_tests.py            run everything
    python3 tests/run_tests.py -v         also show passing tests
"""
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LUCIDC = ROOT / "build" / "lucidc"
CASES = ROOT / "tests" / "cases"
ERROR_LINE = re.compile(r"^\s*\[(\w+)\]\s+(\w+)\s+\(line")


def expectations(path):
    exp = {}
    for line in path.read_text().splitlines()[:4]:
        m = re.match(r"//\s*(?:expect-)?(\w+):\s*(.*)$", line)
        if m:
            exp[m.group(1)] = m.group(2).strip()
    split = lambda s: [x.strip() for x in s.split("|")] if s else []
    return int(exp.get("exit", "0")), split(exp.get("output", "")), split(exp.get("errors", "")), \
           exp.get("stdin", "").replace("\\n", "\n")


def run_compiler(path, stdin=""):
    proc = subprocess.run([str(LUCIDC), str(path)], input=stdin, capture_output=True, text=True, timeout=20)
    lines = proc.stdout.splitlines()
    output, errors, section = [], [], None
    for line in lines:
        if line.startswith("--- "):
            section = "output" if line.startswith("--- Output") else \
                      "errors" if line.startswith("--- Errors") else None
            continue
        if section == "output" and line and line.strip() != "(no output)":
            output.append(line)
        elif section == "errors":
            m = ERROR_LINE.match(line)
            if m:
                errors.append(f"{m.group(1)}:{m.group(2)}")
    return proc.returncode, output, errors


def main():
    if not LUCIDC.exists():
        sys.exit("build/lucidc not found - build first:  cmake --build build")
    verbose = "-v" in sys.argv
    cases = sorted(CASES.rglob("*.lucid"))
    groups, failures = {}, []
    start = time.perf_counter()

    for path in cases:
        *want, stdin = expectations(path)
        want = tuple(want)
        got = run_compiler(path, stdin)
        if want[1] == ["*"]:                      # output deliberately unchecked
            want = (want[0], got[1], want[2])
        group = path.parent.name
        groups.setdefault(group, [0, 0])
        groups[group][1] += 1
        if got == want:
            groups[group][0] += 1
            if verbose:
                print(f"  PASS  {group}/{path.stem}")
        else:
            failures.append((path, want, got))
            print(f"  FAIL  {group}/{path.stem}")
            for label, w, g in zip(("exit", "output", "errors"), want, got):
                if w != g:
                    print(f"          {label}: expected {w!r}, got {g!r}")

    elapsed = time.perf_counter() - start
    print()
    for group, (passed, total) in groups.items():
        print(f"  {group:<10} {passed:>3} / {total}")
    passed = sum(p for p, _ in groups.values())
    print(f"\n  {passed} / {len(cases)} passed in {elapsed:.2f}s")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
