#!/usr/bin/env python3
"""Run DualPadDocGen and verify generated documentation completeness."""

from __future__ import annotations

import pathlib
import subprocess
import sys


EXPECTED = [
    pathlib.Path("docs/generated/context_catalog_zh.md"),
    pathlib.Path("docs/generated/action_sets_zh.md"),
    pathlib.Path("docs/generated/prompt_matrix_zh.md"),
    pathlib.Path("docs/generated/policies_zh.md"),
]


def project_root() -> pathlib.Path:
    current = pathlib.Path.cwd().resolve()
    for candidate in [current, *current.parents]:
        if (candidate / "xmake.lua").exists() and (candidate / "config/DualPadBindings.ini").exists():
            return candidate
    raise SystemExit("cannot locate DualPad project root")


def main() -> int:
    root = project_root()
    result = subprocess.run(["xmake", "run", "DualPadDocGen"], cwd=root)
    if result.returncode != 0:
        return result.returncode

    missing = [str(path) for path in EXPECTED if not (root / path).is_file()]
    if missing:
        for path in missing:
            print(f"missing generated doc: {path}", file=sys.stderr)
        return 1

    for path in EXPECTED:
        content = (root / path).read_text(encoding="utf-8")
        required = [
            "source config root:",
            "manifest hash:",
            "trace schema version:",
            "generator version / command:",
        ]
        for token in required:
            if token not in content:
                print(f"{path} missing provenance token: {token}", file=sys.stderr)
                return 1

    print("generated docs verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
