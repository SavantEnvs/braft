// Fuzzes the stable raft meta file (term + votedfor) as FileBasedSingleMetaStorage::init() loads it.
#include <stdint.h>
#include <string>

#include "braft/raft.h"
#include "braft/raft_meta.h"
#include "harness_util.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    harness::quiet_logging();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    harness::ScratchDir dir;
    if (!harness::write_file(dir.path + "/raft_meta", data, size)) {
        return 0;
    }
    braft::FileBasedSingleMetaStorage storage(dir.path);
    if (storage.init().ok()) {
        int64_t term = 0;
        braft::PeerId votedfor;
        storage.get_term_and_votedfor(&term, &votedfor, braft::VersionedGroupId("fuzz"));
        storage.set_term_and_votedfor(term + 1, votedfor, braft::VersionedGroupId("fuzz"));
    }
    return 0;
}
