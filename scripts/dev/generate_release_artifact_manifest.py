"""Generate a DP5-RC20 release artifact manifest under build/release."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]

CONFIG_FILES = [
    pathlib.Path("config/DualPadDebug.ini"),
    pathlib.Path("config/DualPadBindings.ini"),
    pathlib.Path("config/DualPadMenuPolicy.ini"),
    pathlib.Path("config/controlmap_profiles/DualPadNativeCombo/Interface/Controls/PC/controlmap.txt"),
    pathlib.Path("tools/dinput8_proxy/DualPadDInput8.ini"),
]

RUNTIME_ARTIFACTS = [
    (pathlib.Path("build/bin/DualPad/DualPad.dll"), True),
    (pathlib.Path("build/bin/DualPad/DualPad.pdb"), False),
    (pathlib.Path("build/bin/DualPadDInput8Proxy/dinput8.dll"), True),
    (pathlib.Path("build/bin/DualPadDInput8Proxy/dinput8.pdb"), False),
]

REPO_OWNED_INTERFACE_ARTIFACTS = [
    pathlib.Path("Interface/startmenu.swf"),
]

GENERATED_DOCS = [
    pathlib.Path("docs/generated/context_catalog_zh.md"),
    pathlib.Path("docs/generated/action_sets_zh.md"),
    pathlib.Path("docs/generated/prompt_matrix_zh.md"),
    pathlib.Path("docs/generated/policies_zh.md"),
]

REVIEWED_RELEASE_DOCS = [
    pathlib.Path("docs/releases/dp5_rc20_u3_release_notes_zh.md"),
    pathlib.Path("docs/authoritative-baseline/dp5_rc20_contract_zh.md"),
]


def run_git(args: list[str]) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def sha256(path: pathlib.Path) -> str | None:
    full = ROOT / path
    if not full.is_file():
        return None
    digest = hashlib.sha256()
    with full.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_entry(path: pathlib.Path, *, role: str, required: bool = True) -> dict[str, Any]:
    full = ROOT / path
    exists = full.is_file()
    return {
        "path": path.as_posix(),
        "role": role,
        "required": required,
        "exists": exists,
        "sizeBytes": full.stat().st_size if exists else None,
        "sha256": sha256(path),
    }


def collect_files(paths: list[pathlib.Path], role: str) -> list[dict[str, Any]]:
    return [file_entry(path, role=role) for path in paths]


def collect_runtime_artifacts() -> list[dict[str, Any]]:
    return [
        file_entry(path, role="runtime-build-output", required=required)
        for path, required in RUNTIME_ARTIFACTS
    ]


def manifest() -> dict[str, Any]:
    commit = run_git(["rev-parse", "HEAD"])
    branch = run_git(["branch", "--show-current"])
    dirty = run_git(["status", "--porcelain", "--untracked-files=no"]) != ""

    files = []
    files.extend(collect_runtime_artifacts())
    files.extend(collect_files(CONFIG_FILES, "packaged-config-source"))
    files.extend(collect_files(REPO_OWNED_INTERFACE_ARTIFACTS, "repo-owned-interface-artifact"))
    files.extend(collect_files(GENERATED_DOCS, "generated-doc"))
    files.extend(collect_files(REVIEWED_RELEASE_DOCS, "reviewed-release-doc"))

    return {
        "schemaVersion": 1,
        "name": "DP5-RC20 U3 release artifact manifest",
        "generatedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "source": {
            "commit": commit,
            "branch": branch,
            "trackedWorkingTreeDirty": dirty,
        },
        "support": {
            "supportedRuntime": "Skyrim SE 1.5.97",
            "skseSurface": "CommonLibSSE-NG",
            "unsupportedRuntimePolicy": "fail-closed: do not install upstream hook or controlmap overlay; log explicit reason",
        },
        "installChecks": {
            "cleanInstall": [
                "DualPad.dll is present under SKSE/Plugins.",
                "DualPadDebug.ini, DualPadBindings.ini, DualPadMenuPolicy.ini, and DualPadControlMap.txt are present or intentionally absent with documented defaults.",
                "Optional game-root dinput8.dll and DualPadDInput8.ini are installed only when keyboard-helper bridge output is required.",
            ],
            "overwriteInstall": [
                "Existing user INI files are not overwritten by xmake deploy unless the user replaces them manually.",
                "DualPad.dll, DualPad.pdb, dinput8.dll, and dinput8.pdb may be overwritten by matching build outputs.",
                "Regenerate this manifest after overwrite to compare source commit and artifact hashes.",
            ],
            "rollbackInstall": [
                "Set use_upstream_gamepad_hook=false in DualPadDebug.ini to disable the upstream XInput route.",
                "Remove or restore DualPad.dll and optional dinput8.dll from the previous release bundle.",
                "Restore prior INI files or DualPad.Manifest.lkg.json only as an explicit rollback artifact.",
            ],
        },
        "configFailClosedChecks": [
            "Missing checked-in config files can compile built-in defaults only when both primary config files are absent.",
            "Bad runtime reload keeps the existing active bundle and does not promote a dirty epoch.",
            "Bad startup config without LKG fails startup promotion.",
            "Stale or unsupported LKG schema fails startup promotion.",
        ],
        "deviceLifecycleChecks": [
            "No device: HID reader retries open and emits no dirty input.",
            "Start connected: first open submits reset, resets live input facts, and binds haptics handle.",
            "Hot-plug: later open follows the same reset/fact-reset/device-handle path.",
            "Disconnect: read error submits reset, clears live input facts, clears haptics handle, closes device, then retries.",
            "Reconnect: reopened device starts from software sequence 1 and fresh live input facts.",
        ],
        "nonGoals": [
            "No new runtime phase.",
            "No input_v2 mainline ownership change.",
            "No complete visual icon artwork production.",
            "No DualSense haptics/vibration promotion in this milestone.",
        ],
        "files": files,
    }


def render_markdown(data: dict[str, Any]) -> str:
    source = data["source"]
    support = data["support"]
    lines = [
        "# DP5-RC20 U3 Release Artifact Manifest",
        "",
        f"- Source commit: `{source['commit']}`",
        f"- Source branch: `{source['branch']}`",
        f"- Tracked working tree dirty: `{str(source['trackedWorkingTreeDirty']).lower()}`",
        f"- Supported runtime: {support['supportedRuntime']}",
        f"- Unsupported runtime policy: {support['unsupportedRuntimePolicy']}",
        "",
        "## Files",
        "",
        "| Role | Path | Exists | Size | SHA-256 |",
        "| --- | --- | --- | --- | --- |",
    ]
    for item in data["files"]:
        digest = item["sha256"] or ""
        short_digest = digest[:16] if digest else ""
        size = "" if item["sizeBytes"] is None else str(item["sizeBytes"])
        lines.append(
            f"| {item['role']} | `{item['path']}` | {str(item['exists']).lower()} | {size} | `{short_digest}` |"
        )

    lines.extend([
        "",
        "## Install Checks",
        "",
    ])
    for name, checks in data["installChecks"].items():
        lines.append(f"### {name}")
        lines.extend([f"- {check}" for check in checks])
        lines.append("")

    lines.extend([
        "## Config Fail-Closed Checks",
        "",
        *[f"- {check}" for check in data["configFailClosedChecks"]],
        "",
        "## Device Lifecycle Checks",
        "",
        *[f"- {check}" for check in data["deviceLifecycleChecks"]],
        "",
        "## Non-Goals",
        "",
        *[f"- {item}" for item in data["nonGoals"]],
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="build/release")
    parser.add_argument("--require-build-artifacts", action="store_true")
    parser.add_argument("--expect-clean", action="store_true")
    args = parser.parse_args()

    data = manifest()
    failures: list[str] = []

    if args.expect_clean and data["source"]["trackedWorkingTreeDirty"]:
        failures.append("tracked working tree is dirty")

    if args.require_build_artifacts:
        for item in data["files"]:
            if item["role"] == "runtime-build-output" and item["required"] and not item["exists"]:
                failures.append(f"missing runtime build artifact: {item['path']}")

    output_dir = ROOT / args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "DP5-RC20-U3-release-artifact-manifest.json"
    md_path = output_dir / "DP5-RC20-U3-release-artifact-manifest.md"
    json_path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    md_path.write_text(render_markdown(data), encoding="utf-8")

    print(f"wrote {json_path.relative_to(ROOT).as_posix()}")
    print(f"wrote {md_path.relative_to(ROOT).as_posix()}")

    if failures:
        print("release artifact manifest check failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
