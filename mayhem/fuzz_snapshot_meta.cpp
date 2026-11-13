// Fuzzes loading of a snapshot's file table: the wire form a follower receives
// (load_from_iobuf_as_remote) and the on-disk form (load_from_file -> ProtoBufFile).
#include <stdint.h>
#include <string>
#include <vector>

#include <butil/iobuf.h>

#include "braft/file_system_adaptor.h"
#include "braft/snapshot.h"
#include "harness_util.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    harness::quiet_logging();
    return 0;
}

static void exercise(braft::LocalSnapshotMetaTable& table) {
    std::vector<std::string> files;
    table.list_files(&files);
    for (size_t i = 0; i < files.size(); ++i) {
        braft::LocalFileMeta file_meta;
        table.get_file_meta(files[i], &file_meta);
    }
    (void)table.meta();
    butil::IOBuf out;
    table.save_to_iobuf_as_remote(&out);
    braft::LocalSnapshotMetaTable reloaded;
    reloaded.load_from_iobuf_as_remote(out);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    {
        butil::IOBuf buf;
        buf.append(data, size);
        braft::LocalSnapshotMetaTable table;
        if (table.load_from_iobuf_as_remote(buf) == 0) {
            exercise(table);
        }
    }
    {
        harness::ScratchDir dir;
        const std::string path = dir.path + "/__raft_snapshot_meta";
        if (harness::write_file(path, data, size)) {
            braft::LocalSnapshotMetaTable table;
            if (table.load_from_file(braft::default_file_system(), path) == 0) {
                exercise(table);
            }
        }
    }
    return 0;
}
