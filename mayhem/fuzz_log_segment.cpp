// Fuzzes the on-disk raft log: a segment file as SegmentLogStorage::init() recovers it, then every
// recovered entry is read back (Segment::get, including ConfigurationPBMeta entries).
//
// Input: byte 0 picks the segment kind (even: open `log_inprogress_*`, odd: closed `log_<first>_<last>`),
// byte 1 is the closed segment's last index minus one (the file name must carry it), the rest is the
// segment file. log_meta is the storage's own bookkeeping file, written with ProtoBufFile as the
// storage itself does; without it init() treats the directory as empty and deletes the segment.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

#include "braft/configuration_manager.h"
#include "braft/log.h"
#include "braft/log_entry.h"
#include "braft/protobuf_file.h"
#include "braft/local_storage.pb.h"
#include "harness_util.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    harness::quiet_logging();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) {
        return -1;
    }
    const bool closed = data[0] & 1;
    const int64_t last_index = 1 + data[1];
    data += 2;
    size -= 2;

    harness::ScratchDir dir;
    braft::LogPBMeta meta;
    meta.set_first_log_index(1);
    if (braft::ProtoBufFile(dir.path + "/log_meta").save(&meta, false) != 0) {
        return 0;
    }
    char name[96];
    if (closed) {
        snprintf(name, sizeof(name), "/log_%020" PRId64 "_%020" PRId64, int64_t(1), last_index);
    } else {
        snprintf(name, sizeof(name), "/log_inprogress_%020" PRId64, int64_t(1));
    }
    if (!harness::write_file(dir.path + name, data, size)) {
        return 0;
    }

    braft::ConfigurationManager conf_manager;
    braft::SegmentLogStorage storage(dir.path, false);
    if (storage.init(&conf_manager) != 0) {
        return 0;
    }
    const int64_t first = storage.first_log_index();
    const int64_t last = storage.last_log_index();
    for (int64_t i = first; i <= last && i < first + 512; ++i) {
        (void)storage.get_term(i);
        braft::LogEntry* entry = storage.get_entry(i);
        if (entry != NULL) {
            entry->Release();
        }
    }
    (void)conf_manager.last_configuration();
    return 0;
}
