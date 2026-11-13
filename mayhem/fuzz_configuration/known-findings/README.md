# Known findings: `fuzz_configuration`

Both reproduce with `/mayhem/fuzz_configuration-standalone <file>` and are in `braft::PeerId::parse`
(`src/braft/configuration.h`). Reproducers are kept out of `testsuite/` so they are not replayed as seeds.

## `stack-overflow-ip` — stack-buffer-overflow (CWE-121)

`parse()` does `sscanf(str, "%[^:]%*[:]%d%*[:]%d%*[:]%d", ip_str, ...)` into `char ip_str[64]` with no field
width, so any host part of 64 bytes or more overflows the stack buffer. `PeerId::parse` and
`Configuration::parse_from` take peer strings from CLI/config input, and the configuration also arrives
over RPC in membership-change requests.
Fix: `"%63[^:]"`.

## `invalid-role-enum` — UBSan invalid enum load (CWE-1285)

`int value` is cast with `role = (Role)value;` *before* `role > WITNESS` is checked, so `1.1.1.1:80:0:81`
loads an out-of-range value into an enum, which is undefined behavior. The range check that follows is
meant to reject it. Fix: range-check `value` as an int before the cast.

## Relaxed UBSan checks (build.sh)

The braft library is built with `-fno-sanitize=alignment,pointer-overflow`: brpc's `butil::DirReader`
casts its getdents buffer to `linux_dirent*` at an unaligned offset, and `SegmentLogStorage::list_segments`
(`log.cpp:1011`) computes `name + (strlen(name) - 4)`, which wraps for `.` and `..`. Both fire on every
directory listing, so they would abort every input of the on-disk targets.
