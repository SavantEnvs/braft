#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# braft's own gtest binaries (built by build.sh into mayhem/build-tests/test). Each asserts known-answer
# values (configuration parsing, checksums, log/snapshot/meta storage round trips...). A binary that
# produces no gtest summary counts as one failure, so a neutered program cannot pass.
TESTS="test_ballot test_checksum test_configuration test_log_entry test_memory_storage
       test_protobuf_file test_util test_meta test_storage test_log test_snapshot"
BIN="$SRC/mayhem/build-tests/test"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
passed=0; failed=0; skipped=0
for t in $TESTS; do
  if [ ! -x "$BIN/$t" ]; then echo "missing test binary $BIN/$t (build.sh should have built it)" >&2; failed=$((failed+1)); continue; fi
  mkdir -p "$WORK/$t"
  out="$WORK/$t.log"
  (cd "$WORK/$t" && timeout 300 "$BIN/$t" >"$out" 2>&1)
  p=$(sed -n 's/^\[  PASSED  \] \([0-9]*\) test.*/\1/p' "$out" | tail -1)
  f=$(sed -n 's/^\[  FAILED  \] \([0-9]*\) test.*/\1/p' "$out" | tail -1)
  s=$(sed -n 's/^\[  SKIPPED \] \([0-9]*\) test.*/\1/p' "$out" | tail -1)
  ran=$(sed -n 's/^\[==========\] \([0-9]*\) tests\? from .* ran\..*/\1/p' "$out" | tail -1)
  if [ -z "$ran" ]; then echo "$t: no gtest summary" >&2; tail -5 "$out" >&2; failed=$((failed+1)); continue; fi
  passed=$((passed+${p:-0})); failed=$((failed+${f:-0})); skipped=$((skipped+${s:-0}))
  echo "$t: ran=$ran passed=${p:-0} failed=${f:-0} skipped=${s:-0}"
done
emit_ctrf "gtest" "$passed" "$failed" "$skipped"
