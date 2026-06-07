#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[install]${NC} $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC} $*"; }
error() { echo -e "${RED}[error]${NC} $*"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect install mode: system (needs sudo) or user (~/.dtp)
MODE="user"
if [[ "$1" == "--system" ]]; then
    MODE="system"
fi

# Check for C++17 compiler
CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null 2>&1 || error "C++ compiler not found. Install g++ or set CXX."

info "Building dtpc with $CXX ..."
cd "$SCRIPT_DIR"
"$CXX" -std=c++17 -O3 -march=native -Wall -Wextra \
       -Iinclude src/dtpc.cpp \
       -o dtpc \
       -lstdc++fs \
       2>&1 || error "Build failed."
info "Build successful."

if [[ "$MODE" == "system" ]]; then
    INSTALL_BIN="/usr/local/bin/dtpc"
    INSTALL_LIB="/usr/local/share/dtp/stdlib"
    info "Installing to $INSTALL_BIN (may need sudo) ..."
    sudo install -m 755 dtpc "$INSTALL_BIN"
    sudo mkdir -p "$INSTALL_LIB"
    sudo cp -r stdlib/. "$INSTALL_LIB/"
    info "System install complete."
    info "  binary : $INSTALL_BIN"
    info "  stdlib : $INSTALL_LIB"
else
    DTP_HOME="$HOME/.dtp"
    DTP_BIN="$DTP_HOME/dtpc"
    DTP_LIB="$DTP_HOME/stdlib"
    mkdir -p "$DTP_LIB"
    cp dtpc "$DTP_BIN"
    cp -r stdlib/. "$DTP_LIB/"
    info "User install complete."
    info "  binary : $DTP_BIN"
    info "  stdlib : $DTP_LIB"

    # Add to PATH if not already there
    PROFILE=""
    if [[ -f "$HOME/.bashrc" ]]; then PROFILE="$HOME/.bashrc"; fi
    if [[ -f "$HOME/.zshrc"  ]]; then PROFILE="$HOME/.zshrc";  fi

    PATH_LINE='export PATH="$HOME/.dtp:$PATH"'
    if [[ -n "$PROFILE" ]]; then
        if ! grep -qF '.dtp' "$PROFILE" 2>/dev/null; then
            echo "" >> "$PROFILE"
            echo "# DTP compiler" >> "$PROFILE"
            echo "$PATH_LINE" >> "$PROFILE"
            info "Added PATH entry to $PROFILE"
        else
            info "PATH entry already present in $PROFILE"
        fi
    else
        warn "Could not find .bashrc or .zshrc — add this to your shell profile manually:"
        warn "  $PATH_LINE"
    fi

    # Activate in current session
    export PATH="$DTP_HOME:$PATH"
fi

echo ""
info "dtpc is ready. Try:"
echo "  dtpc --help"
echo "  dtpc hello.dtp -o hello"
