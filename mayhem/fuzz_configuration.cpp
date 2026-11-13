// Fuzzes braft's peer / configuration string parsing (PeerId::parse, Configuration::parse_from)
// and the set operations that run on the parsed result.
#include <stdint.h>
#include <sstream>
#include <string>
#include <vector>

#include "braft/configuration.h"
#include "harness_util.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    harness::quiet_logging();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);

    braft::PeerId peer;
    if (peer.parse(input) == 0) {
        braft::PeerId again;
        again.parse(peer.to_string());
        (void)peer.is_empty();
        (void)peer.is_witness();
    }

    braft::Configuration conf;
    if (conf.parse_from(input) == 0) {
        std::ostringstream os;
        os << conf;

        braft::Configuration reparsed;
        reparsed.parse_from(os.str());
        (void)conf.equals(reparsed);

        std::vector<braft::PeerId> peers;
        conf.list_peers(&peers);
        (void)conf.contains(peers);
        (void)conf.equals(peers);

        braft::Configuration included, excluded;
        conf.diffs(reparsed, &included, &excluded);
        if (!peers.empty()) {
            conf.remove_peer(peers.front());
            conf.add_peer(peers.back());
        }
    }
    return 0;
}
