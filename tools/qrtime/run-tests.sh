#!/bin/bash
# Runs a named set of tests/*.sh on a Linux host and prints PASS/FAIL/SKIP.
#
#   bash tools/qrtime/run-tests.sh                 # the QR TIME gate set
#   bash tools/qrtime/run-tests.sh ltv m3u aovivo  # specific tests
#   QRT_TEST_LOGS=dir bash tools/qrtime/run-tests.sh
#
# macOS-only scripts are compiled through tools/qrtime/hostcc/cc (see there).
# A script that prints SKIP does not count as passed. Exit code is non-zero
# when any test fails.
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$PWD
export PATH="$ROOT/tools/qrtime/hostcc:$PATH"

GATE_FILE="$ROOT/tools/qrtime/gate-tests.txt"
if [ "$#" -gt 0 ]; then
  tests=("$@")
else
  mapfile -t tests < <(grep -v '^\s*#' "$GATE_FILE" | grep -v '^\s*$')
fi

LOGS="${QRT_TEST_LOGS:-$(mktemp -d "${TMPDIR:-/tmp}/qrtime-tests.XXXXXX")}"
mkdir -p "$LOGS"
pass=0 fail=0 skip=0
failed=()
for t in "${tests[@]}"; do
  # "name:seconds" gives one slow test its own limit (default TEST_TIMEOUT).
  lim="${TEST_TIMEOUT:-300}"
  case "$t" in *:*) lim="${t##*:}"; t="${t%%:*}";; esac
  t="${t%.sh}"
  f="tests/$t.sh"
  if [ ! -f "$f" ]; then echo "MISSING $t"; fail=$((fail+1)); failed+=("$t"); continue; fi
  if timeout -k 5 "$lim" bash "$f" >"$LOGS/$t.log" 2>&1; then
    if grep -Eq '(^|[^A-Za-z])SKIP([^A-Za-z]|$)' "$LOGS/$t.log"; then
      echo "SKIP  $t"; skip=$((skip+1))
    else
      echo "PASS  $t"; pass=$((pass+1))
    fi
  else
    echo "FAIL  $t  ($LOGS/$t.log)"; fail=$((fail+1)); failed+=("$t")
  fi
done
echo "---"
echo "pass=$pass fail=$fail skip=$skip  logs=$LOGS"
[ "$fail" = 0 ]
