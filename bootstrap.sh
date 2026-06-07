#!/usr/bin/env bash
# Provision the toolchain checks, python deps, and vendor submodules for nt_doom.
# Idempotent: safe to re-run in fresh clones and in worktrees.
set -euo pipefail

REQUIRED_BINS=(git make python3 arm-none-eabi-c++ arm-none-eabi-gcc arm-none-eabi-ld arm-none-eabi-objcopy arm-none-eabi-nm arm-none-eabi-objdump)
MISSING=()
for bin in "${REQUIRED_BINS[@]}"; do
    command -v "$bin" >/dev/null 2>&1 || MISSING+=("$bin")
done
if [ "${#MISSING[@]}" -ne 0 ]; then
    echo "Missing required tools: ${MISSING[*]}" >&2
    echo "  macOS: xcode-select --install; brew install --cask gcc-arm-embedded; brew install python3" >&2
    echo "  Debian/Ubuntu: apt-get install gcc-arm-none-eabi python3 python3-pip make" >&2
    exit 1
fi

# Python deps for the hardware-deploy and screenshot scripts (mido + rtmidi).
# --user keeps out of the system tree; --break-system-packages covers PEP 668.
if [ -f requirements.txt ]; then
    pip3 install --user -r requirements.txt \
        || pip3 install --user --break-system-packages -r requirements.txt \
        || echo "WARNING: pip install failed; hardware-deploy scripts need mido + python-rtmidi." >&2
fi

# vendor/distingNT_API: the NT plug-in ABI (full submodule).
git submodule update --init --recursive vendor/distingNT_API

# vendor/llvm-project sparse-checkout: compiler-rt/lib/builtins only.
# A plain git submodule update does NOT carry sparse-checkout config, so the
# bootstrap applies it explicitly. The full tree is multi-GB; the sparse tree
# is ~2 MB. Block is idempotent.
SUBMOD=vendor/llvm-project
URL=$(git config -f .gitmodules submodule.$SUBMOD.url)
TAG=$(git config -f .gitmodules submodule.$SUBMOD.branch)

if [ ! -e "$SUBMOD/.git" ]; then
    git clone --no-checkout --depth=1 --filter=blob:none -b "$TAG" "$URL" "$SUBMOD"
    git -C "$SUBMOD" sparse-checkout init --cone
    git -C "$SUBMOD" sparse-checkout set compiler-rt/lib/builtins
    git -C "$SUBMOD" checkout "$TAG"
    git submodule absorbgitdirs "$SUBMOD" || true
else
    git -C "$SUBMOD" sparse-checkout reapply || true
fi

echo "bootstrap: toolchain OK, submodules provisioned."
echo "  vendor/distingNT_API : $(git -C vendor/distingNT_API rev-parse --short HEAD 2>/dev/null || echo missing)"
echo "  vendor/llvm-project  : sparse compiler-rt/lib/builtins @ $TAG"
