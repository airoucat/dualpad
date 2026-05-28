#!/bin/sh

graphify_repo_root() {
    git rev-parse --show-toplevel 2>/dev/null || pwd
}

graphify_detect_python() {
    GRAPHIFY_PYTHON=""
    REPO_ROOT=$(graphify_repo_root)

    if [ -f "$REPO_ROOT/graphify-out/.graphify_python" ]; then
        GRAPHIFY_PYTHON=$(tr -d '\r\n' < "$REPO_ROOT/graphify-out/.graphify_python")
        if [ -n "$GRAPHIFY_PYTHON" ] && ! "$GRAPHIFY_PYTHON" -c "import graphify" 2>/dev/null; then
            GRAPHIFY_PYTHON=""
        fi
    fi

    if [ -z "$GRAPHIFY_PYTHON" ]; then
        if command -v python3 >/dev/null 2>&1 && python3 -c "import graphify" 2>/dev/null; then
            GRAPHIFY_PYTHON="python3"
        elif command -v python >/dev/null 2>&1 && python -c "import graphify" 2>/dev/null; then
            GRAPHIFY_PYTHON="python"
        else
            return 1
        fi
    fi

    printf '%s\n' "$GRAPHIFY_PYTHON"
}

graphify_run_rebuild() {
    REASON="$1"
    REPO_ROOT=$(graphify_repo_root)
    GRAPHIFY_PYTHON=$(graphify_detect_python) || return 0

    cd "$REPO_ROOT" || return 1
    if [ ! -f "scripts/dev/setup_graphify_local.py" ]; then
        echo "[graphify] scripts/dev/setup_graphify_local.py not found - skipping"
        return 0
    fi

    "$GRAPHIFY_PYTHON" scripts/dev/setup_graphify_local.py rebuild --reason "$REASON"
}
