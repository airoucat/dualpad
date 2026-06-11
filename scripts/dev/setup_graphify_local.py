from __future__ import annotations

import argparse
import os
import stat
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
HOOK_DIR = REPO_ROOT / ".githooks"
GRAPHIFYY_PACKAGE = "graphifyy==0.4.14"


def ensure_graphify() -> None:
    try:
        import graphify  # noqa: F401
        return
    except ImportError:
        print(f"[graphify setup] graphify 未安装，使用当前 Python 自动安装 {GRAPHIFYY_PACKAGE} ...")
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "--user", GRAPHIFYY_PACKAGE],
            cwd=REPO_ROOT,
            check=True,
        )
        import graphify  # noqa: F401


def ensure_hook_permissions() -> None:
    if os.name == "nt" or not HOOK_DIR.exists():
        return
    for path in HOOK_DIR.iterdir():
        if path.is_file():
            mode = path.stat().st_mode
            path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def ensure_codex_local_hook() -> None:
    subprocess.run(
        [sys.executable, "-m", "graphify", "codex", "install"],
        cwd=REPO_ROOT,
        check=True,
    )


def configure_hooks_path() -> None:
    subprocess.run(
        ["git", "config", "--local", "core.hooksPath", ".githooks"],
        cwd=REPO_ROOT,
        check=True,
    )


def rebuild_code_graph(reason: str) -> int:
    ensure_graphify()
    from graphify.watch import _rebuild_code

    print(f"[graphify setup] 触发代码图重建，来源: {reason}")
    ok = _rebuild_code(REPO_ROOT)
    if ok:
        out = REPO_ROOT / "graphify-out"
        out.mkdir(exist_ok=True)
        (out / ".graphify_python").write_text(sys.executable, encoding="utf-8")
        return 0
    return 1


def install_local_automation(skip_rebuild: bool) -> int:
    ensure_graphify()
    ensure_hook_permissions()
    configure_hooks_path()
    ensure_codex_local_hook()

    print("[graphify setup] 已将 Git hooks 切换到仓库内 .githooks/")
    print("[graphify setup] commit / checkout / merge / rebase 后会自动重建本地代码图")

    graph_json = REPO_ROOT / "graphify-out" / "graph.json"
    if skip_rebuild:
        return 0
    if graph_json.exists():
        print("[graphify setup] 已存在 graphify-out/graph.json，跳过首轮重建")
        return 0
    return rebuild_code_graph("initial-install")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Install repo-local Graphify automation for DualPad."
    )
    subparsers = parser.add_subparsers(dest="command")

    install_parser = subparsers.add_parser(
        "install", help="configure repo-local hooks and local Codex hook"
    )
    install_parser.add_argument(
        "--skip-rebuild",
        action="store_true",
        help="configure hooks only, do not build graphify-out/graph.json on first install",
    )

    rebuild_parser = subparsers.add_parser("rebuild", help="rebuild code graph only")
    rebuild_parser.add_argument(
        "--reason",
        default="manual",
        help="human-readable trigger label for logs",
    )

    parser.set_defaults(command="install", skip_rebuild=False)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "rebuild":
        return rebuild_code_graph(args.reason)
    return install_local_automation(args.skip_rebuild)


if __name__ == "__main__":
    raise SystemExit(main())
