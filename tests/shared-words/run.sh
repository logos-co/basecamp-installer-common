#!/usr/bin/env bash
# The shared-words checker: finds copies, joins split phrases, skips vendored
# copies, honours the allowlist.
set -u
cd "$(dirname "$0")/../.."
check=tools/shared-words-check
fail() { echo "FAIL: $*"; cat /tmp/shared-words.out 2>/dev/null; exit 1; }
expect() {
    local want=$1; shift
    python3 "$check" "$@" > "$TMPDIR/out" 2>&1
    local got=$?
    cp "$TMPDIR/out" /tmp/shared-words.out 2>/dev/null || true
    [ "$got" -eq "$want" ] || fail "exit $got, wanted $want: $*"
}
TMPDIR=${TMPDIR:-/tmp}

expect 1 qml/Installer tests/shared-words/copied
grep -q 'Copied.qml:4: "Finding peers…" is in the shared Stages.js' "$TMPDIR/out" || fail "the copy"
grep -q 'Copied.qml:5: "Downloads are set to Logos Storage only' "$TMPDIR/out" || fail "the split phrase"
expect 0 qml/Installer tests/shared-words/clean
expect 1 qml/Installer tests/shared-words/vendored
expect 0 qml/Installer tests/shared-words/vendored --skip tests/shared-words/vendored/Installer
expect 0 qml/Installer tests/shared-words --skip tests/shared-words/vendored/Installer \
    --allow tests/shared-words/allow.txt
expect 2 qml/Installer
echo "shared-words-check: all cases pass"
