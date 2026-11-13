// Fuzzes the segmented file payload a snapshot sender attaches to a GetFileResponse and the receiver
// (RemoteFileCopier) splits back with FileSegData::next.
#include <stdint.h>

#include <butil/iobuf.h>

#include "braft/util.h"
#include "harness_util.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    harness::quiet_logging();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    butil::IOBuf attachment;
    attachment.append(data, size);

    braft::FileSegData reader(attachment);
    braft::FileSegData rewriter;
    uint64_t offset = 0;
    butil::IOBuf segment;
    while (reader.next(&offset, &segment) != 0) {
        rewriter.append(segment, offset);
        segment.clear();
    }
    (void)rewriter.data().size();
    return 0;
}
