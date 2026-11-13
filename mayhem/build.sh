#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

DEPS=/opt/toolchains/deps
BRPC="$DEPS/brpc-1.8.0"
GTEST="$DEPS/googletest-1.12.1"
TARGETS="fuzz_configuration fuzz_snapshot_meta fuzz_raft_meta fuzz_file_seg_data fuzz_log_segment"

# CMake takes the compiler from the exported CC/CXX: passing -DCMAKE_*_COMPILER makes a re-run see a "changed"
# compiler and drop the cache, including the prefix path.
# 1) braft itself (static), sanitized + DWARF 3, with upstream's own CMake. Build dirs stay under mayhem/
#    (gitignored) so the tree is clean for `git clean -ffdX` rebuilds.
#    -fsanitize=fuzzer-no-link puts SanCov on the library itself (not just the harness TU).
#    UBSan's `alignment` and `pointer-overflow` checks are relaxed (everything else stays halting): both
#    fire on every directory listing the storage code does, i.e. on every input of the on-disk targets.
#    alignment: brpc's header-only butil::DirReader casts its getdents buffer to `linux_dirent*` at an
#    unaligned offset. pointer-overflow: SegmentLogStorage::list_segments (log.cpp:1011) computes
#    `name + (strlen(name) - 4)`, which wraps for the "." and ".." entries.
UBSAN_RELAX="-fno-sanitize=alignment,pointer-overflow"
cmake -S . -B mayhem/build-fuzz -G Ninja -DCMAKE_BUILD_TYPE= \
      -DCMAKE_PREFIX_PATH="$BRPC" -DWITH_DEBUG_SYMBOLS=OFF \
      -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $UBSAN_RELAX -fsanitize=fuzzer-no-link $DEBUG_FLAGS" \
      -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $UBSAN_RELAX -fsanitize=fuzzer-no-link $DEBUG_FLAGS"
cmake --build mayhem/build-fuzz -j"$MAYHEM_JOBS" --target braft-static

# 2) Harnesses: libFuzzer binary + standalone reproducer each, both with LSan switched off at build time.
#    The defines mirror upstream's CMAKE_CPP_FLAGS (GFLAGS_NS is what its configure step detects).
HFLAGS="$UBSAN_RELAX -std=c++11 -O2 -fPIC -DBRPC_WITH_GLOG=0 -DGFLAGS_NS=google -DBTHREAD_USE_FAST_PTHREAD_MUTEX \
  -D__const__=__unused__ -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS \
  -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D__STRICT_ANSI__ -DNDEBUG -Wno-reserved-user-defined-literal"
HINC="-I$SRC/src -I$SRC/mayhem/build-fuzz -I$BRPC/include"
LIBS="$SRC/mayhem/build-fuzz/output/lib/libbraft.a $BRPC/lib/libbrpc.a \
  -lprotobuf -lleveldb -lgflags -lsnappy -lssl -lcrypto -lz -ldl -lrt -lpthread"

mkdir -p /mayhem/obj
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o /mayhem/obj/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /mayhem/obj/standalone_main.o
for t in $TARGETS; do
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $HFLAGS $HINC $LIB_FUZZING_ENGINE \
      "$SRC/mayhem/$t.cpp" /mayhem/obj/lsan_off.o $LIBS -o "/mayhem/$t"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $HFLAGS $HINC \
      "$SRC/mayhem/$t.cpp" /mayhem/obj/standalone_main.o /mayhem/obj/lsan_off.o $LIBS -o "/mayhem/$t-standalone"
done

# 3) braft's own unit tests, normal flags, separate tree. Only the fast deterministic ones that test.sh runs.
cmake -S . -B mayhem/build-tests -G Ninja \
      -DCMAKE_PREFIX_PATH="$BRPC;$GTEST" -DBUILD_UNIT_TESTS=ON \
      -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" \
      -DCMAKE_CXX_FLAGS="-isystem $GTEST/include $COVERAGE_FLAGS"
cmake --build mayhem/build-tests -j"$MAYHEM_JOBS" --target \
      test_ballot test_checksum test_configuration test_log_entry test_memory_storage \
      test_protobuf_file test_util test_meta test_storage test_log test_snapshot
