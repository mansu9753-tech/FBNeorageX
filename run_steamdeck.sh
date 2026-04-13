#!/bin/bash
# FBNeoRageX - Steam Deck Launcher with Auto-Setup
# Uses venv to avoid SteamOS PEP 668 "externally-managed-environment" error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
LOG_FILE="$SCRIPT_DIR/launch.log"
VENV_DIR="$SCRIPT_DIR/.venv"
VENV_PYTHON="$VENV_DIR/bin/python3"
VENV_PIP="$VENV_DIR/bin/pip"

log() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

# ── Check venv + dependencies ─────────────────────────────────────
check_deps() {
    [ -f "$VENV_PYTHON" ] && \
    "$VENV_PYTHON" -c "import PySide6; import OpenGL; import sounddevice; import numpy" 2>/dev/null
}

# ── First-time setup ──────────────────────────────────────────────
run_setup() {
    log "=== First-time setup (venv) ==="

    # Create venv (avoids PEP 668 externally-managed-environment error)
    if [ ! -f "$VENV_PYTHON" ]; then
        log "Creating venv at $VENV_DIR ..."
        python3 -m venv "$VENV_DIR"
        if [ $? -ne 0 ]; then
            log "ERROR: python3 -m venv failed"
            return 1
        fi
    fi

    log "Installing packages into venv..."
    "$VENV_PIP" install --upgrade pip --quiet
    "$VENV_PIP" install PySide6 PyOpenGL Pillow sounddevice numpy --quiet

    if check_deps; then
        log "Setup complete!"
        return 0
    else
        log "ERROR: Setup failed — packages not importable after install"
        return 1
    fi
}

# ── Main ──────────────────────────────────────────────────────────
echo "=== FBNeoRageX ===" > "$LOG_FILE"
log "Script dir: $SCRIPT_DIR"
log "Python: $(python3 --version 2>&1)"

if ! check_deps; then
    log "Dependencies not found. Running setup..."
    if ! run_setup; then
        log "FATAL: Setup failed. See $LOG_FILE"
        if command -v kdialog &>/dev/null; then
            kdialog --error "FBNeoRageX setup failed.\nSee: $LOG_FILE" 2>/dev/null
        elif command -v zenity &>/dev/null; then
            zenity --error --text="FBNeoRageX setup failed.\nSee: $LOG_FILE" 2>/dev/null
        fi
        exit 1
    fi
fi

# ── Launch ────────────────────────────────────────────────────────
MAIN_SCRIPT="$SCRIPT_DIR/FBNeoRageX_v1.7.py"
if [ ! -f "$MAIN_SCRIPT" ]; then
    log "ERROR: FBNeoRageX_v1.7.py not found in $SCRIPT_DIR"
    exit 1
fi

export LD_LIBRARY_PATH="$SCRIPT_DIR:$LD_LIBRARY_PATH"

if [ -n "$WAYLAND_DISPLAY" ]; then
    export QT_QPA_PLATFORM=wayland
    log "Display: Wayland"
elif [ -n "$DISPLAY" ]; then
    export QT_QPA_PLATFORM=xcb
    log "Display: X11 ($DISPLAY)"
else
    export DISPLAY=:1
    export QT_QPA_PLATFORM=xcb
    log "Display: fallback X11 :1"
fi

export QT_XCB_NO_MITSHM=1
export QT_SCALE_FACTOR=1

cd "$SCRIPT_DIR"
log "Launching with venv python..."

export PYTHONFAULTHANDLER=1
"$VENV_PYTHON" -X faulthandler "$MAIN_SCRIPT" "$@" >> "$LOG_FILE" 2>&1
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    log "ERROR: Program exited with code $EXIT_CODE"
    log "Check $LOG_FILE for details"
fi
