#!/bin/sh
# check-nvml-symbols.sh
#
# Requirement R3 (requirements-nvml-enumeration-compat.md): detect NVML
# symbols that the installed nvidia-container-toolkit binaries reference but
# the fake NVML stub does not export — BEFORE they fail at runtime as
# "exit 127, symbol lookup error" with no logs (lazy ELF binding kills the
# process at first call of an unimplemented go-nvml cgo symbol).
#
# Usage: ./check-nvml-symbols.sh [stub.so] [toolkit-binary...]
#   stub.so            defaults to the loader-resolved libnvidia-ml.so.1
#   toolkit-binary     defaults to /usr/bin/nvidia-container-runtime and
#                      /usr/bin/nvidia-ctk
#
# Verdict rules:
#   - A missing symbol that is on the nvcdi device-enumeration path
#     (REACHABLE below) is a FAIL (exit 1).
#   - Any other missing symbol is informational only (the toolkit carries
#     ~428 undefined nvml* symbols; full coverage is not necessary).
#   - REACHABLE is maintained per toolkit version: diff the toolkit's
#     pkg/nvcdi/*.go and the vendored go-nvlib/pkg/nvlib/device/*.go Device
#     method calls when updating nvidia-container-toolkit.
set -u

REACHABLE="nvmlInit nvmlInit_v2 nvmlInitWithFlags nvmlShutdown nvmlDeviceGetCount nvmlDeviceGetCount_v2
nvmlDeviceGetHandleByIndex nvmlDeviceGetHandleByIndex_v2 nvmlDeviceGetHandleByUUID nvmlDeviceGetHandleByUUIDV
nvmlDeviceGetUUID nvmlDeviceGetName nvmlDeviceGetIndex nvmlDeviceGetMinorNumber nvmlDeviceIsMigDeviceHandle
nvmlDeviceGetMigMode nvmlDeviceGetMaxMigDeviceCount nvmlDeviceGetMigCapability nvmlDeviceGetMigDeviceHandleByIndex
nvmlDeviceGetDeviceHandleFromMigDeviceHandle nvmlDeviceGetGpuInstanceId nvmlDeviceGetComputeInstanceId"

STUB="${1:-}"
# shift is a POSIX special builtin: dash exits on a failed shift even with
# `|| true`, so guard it rather than relying on error suppression.
if [ $# -gt 0 ]; then
    shift
fi
if [ -z "$STUB" ]; then
    # Prefer the native-architecture libnvidia-ml.so.1 entry: on Debian/Ubuntu
    # a compat32 (i386) entry also exists and is the REAL 32-bit driver
    # library, not this stub — comparing against it would silently pass. The
    # arch tag is derived from the compiler triple exactly like the Makefile
    # install logic (x86_64-* -> x86-64, aarch64-* -> aarch64).
    TRIPLE=$( (cc -dumpmachine 2>/dev/null) || (gcc -dumpmachine 2>/dev/null) )
    case "$TRIPLE" in
        x86_64-*)  ARCH_TAG="x86-64" ;;
        aarch64-*) ARCH_TAG="aarch64" ;;
        *)         ARCH_TAG="${TRIPLE%%-*}" ;;
    esac
    STUB=$(ldconfig -p 2>/dev/null | grep 'libnvidia-ml\.so\.1 ' | grep ",$ARCH_TAG" | tail -n1 | sed -n 's/.*=> \(.*\)/\1/p')
    if [ -z "$STUB" ]; then
        STUB=$(ldconfig -p 2>/dev/null | grep 'libnvidia-ml\.so\.1 ' | head -n1 | sed -n 's/.*=> \(.*\)/\1/p')
    fi
    if [ -z "$STUB" ]; then
        echo "SKIP: no libnvidia-ml.so.1 found via ldconfig; pass a stub path as the first argument" >&2
        exit 2
    fi
fi

BINARIES="$*"
if [ -z "$BINARIES" ]; then
    BINARIES="/usr/bin/nvidia-container-runtime /usr/bin/nvidia-ctk"
fi

PRESENT=""
for b in $BINARIES; do
    if [ -x "$b" ]; then
        PRESENT="$PRESENT $b"
    else
        echo "SKIP: toolkit binary not found: $b" >&2
    fi
done
if [ -z "$PRESENT" ]; then
    echo "SKIP: no toolkit binary found to check; pass binary paths as arguments" >&2
    exit 2
fi

echo "stub: $STUB"

# Union of NVML undefined symbols across all present toolkit binaries.
# nm -D undefined lines come in two layouts — "ADDR U sym" (some ET_DYN
# binaries) and "        U sym" (PIE) — so match " U " textually and take the
# last field instead of a fixed column.
REQUIRED=$(for b in $PRESENT; do
    [ -x "$b" ] && nm -D "$b" 2>/dev/null | grep ' U ' | awk '{print $NF}' | grep '^nvml'
done | sort -u)

if [ -z "$REQUIRED" ]; then
    echo "FAIL: no NVML undefined symbols found in any toolkit binary; nothing to compare" >&2
    exit 1
fi

EXPORTED=$(nm -D "$STUB" 2>/dev/null | grep -E ' [TWi] ' | awk '{print $NF}' | grep '^nvml' | sort -u)

fail=0
echo "required nvml symbols: $(echo "$REQUIRED" | wc -l); exported by stub: $(echo "$EXPORTED" | wc -l)"
echo ""

echo "== missing (reachable: nvcdi device-enumeration path) =="
for sym in $REQUIRED; do
    if ! echo "$EXPORTED" | grep -qx "$sym"; then
        if echo "$REACHABLE" | tr ' \n' '\n\n' | grep -qx "$sym"; then
            echo "  MISSING-REACHABLE: $sym"
            fail=1
        fi
    fi
done
[ "$fail" -eq 0 ] && echo "  (none)"

echo ""
echo "== missing (informational: not on the enumerated path) =="
other=0
shown=0
for sym in $REQUIRED; do
    if ! echo "$EXPORTED" | grep -qx "$sym"; then
        if ! echo "$REACHABLE" | tr ' \n' '\n\n' | grep -qx "$sym"; then
            other=$((other + 1))
            if [ "$shown" -lt 20 ]; then
                echo "  missing: $sym"
                shown=$((shown + 1))
            fi
        fi
    fi
done
if [ "$other" -eq 0 ]; then
    echo "  (none)"
elif [ "$other" -gt "$shown" ]; then
    echo "  ... and $((other - shown)) more (full coverage not required; see REACHABLE)"
fi

if [ "$fail" -eq 1 ]; then
    echo ""
    echo "FAIL: reachable NVML symbols are missing from the stub (exit 127 at runtime)"
    exit 1
fi
echo ""
echo "PASS: all reachable NVML symbols are exported by the stub"
exit 0
