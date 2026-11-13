// Regenerates the starter seeds under mayhem/<target>/testsuite/ from braft's own writers, so the
// checksums inside log segments are valid. Not part of build.sh: compile it with the harness flags and
// libs from build.sh (plus -Imayhem), then run: gen_seeds <repo>/mayhem
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <braft/configuration.h>
#include <braft/file_system_adaptor.h>
#include <braft/local_storage.pb.h>
#include <braft/log.h>
#include <braft/log_entry.h>
#include <braft/protobuf_file.h>
#include <braft/snapshot.h>
#include <braft/util.h>
#include "harness_util.h"

static std::string g_root;

static void put(const std::string& target, const std::string& name, const std::string& bytes) {
    const std::string dir = g_root + "/" + target + "/testsuite";
    mkdir((g_root + "/" + target).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    std::ofstream(dir + "/" + name, std::ios::binary) << bytes;
}

static std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void configuration_seeds() {
    put("fuzz_configuration", "peer_basic", "127.0.0.1:8100");
    put("fuzz_configuration", "peer_idx", "10.1.2.3:8200:1");
    put("fuzz_configuration", "peer_witness", "192.168.0.9:8300:2:1");
    put("fuzz_configuration", "peer_host", "localhost:9000");
    put("fuzz_configuration", "conf_three", "127.0.0.1:8100:0,127.0.0.1:8101:0,127.0.0.1:8102:0");
    put("fuzz_configuration", "conf_spaces", "10.0.0.1:80, 10.0.0.2:80 ,10.0.0.3:81:1");
}

static void raft_meta_seeds() {
    const char* votes[] = {"127.0.0.1:8100:0", "10.0.0.7:9000:1", "0.0.0.0:0:0"};
    const int64_t terms[] = {1, 42, 1000000007};
    for (int i = 0; i < 3; ++i) {
        harness::ScratchDir dir;
        braft::StablePBMeta meta;
        meta.set_term(terms[i]);
        meta.set_votedfor(votes[i]);
        braft::ProtoBufFile(dir.path + "/raft_meta").save(&meta, false);
        char name[32];
        snprintf(name, sizeof(name), "meta_%d", i);
        put("fuzz_raft_meta", name, slurp(dir.path + "/raft_meta"));
    }
}

static void snapshot_meta_seeds() {
    for (int variant = 0; variant < 3; ++variant) {
        braft::LocalSnapshotMetaTable table;
        braft::SnapshotMeta meta;
        meta.set_last_included_index(100 * (variant + 1));
        meta.set_last_included_term(3 + variant);
        meta.add_peers("127.0.0.1:8100:0");
        meta.add_peers("127.0.0.1:8101:0");
        if (variant == 2) {
            meta.add_old_peers("127.0.0.1:8100:0");
        }
        table.set_meta(meta);
        for (int f = 0; f <= variant; ++f) {
            braft::LocalFileMeta fm;
            fm.set_checksum(f == 0 ? "1234abcd" : "deadbeef");
            fm.set_user_meta(std::string("user-") + char('a' + f));
            fm.set_source(f % 2 ? braft::FILE_SOURCE_REFERENCE : braft::FILE_SOURCE_LOCAL);
            char fname[32];
            snprintf(fname, sizeof(fname), "data_%d.sst", f);
            table.add_file(fname, fm);
        }
        char name[32];
        snprintf(name, sizeof(name), "remote_%d", variant);
        butil::IOBuf buf;
        table.save_to_iobuf_as_remote(&buf);
        put("fuzz_snapshot_meta", name, buf.to_string());
        harness::ScratchDir dir;
        table.save_to_file(braft::default_file_system(), dir.path + "/__raft_snapshot_meta");
        snprintf(name, sizeof(name), "file_%d", variant);
        put("fuzz_snapshot_meta", name, slurp(dir.path + "/__raft_snapshot_meta"));
    }
}

static void file_seg_seeds() {
    for (int variant = 0; variant < 3; ++variant) {
        braft::FileSegData writer;
        for (int s = 0; s <= variant * 2; ++s) {
            butil::IOBuf seg;
            std::string payload(16 + 40 * s, char('A' + s));
            seg.append(payload);
            writer.append(seg, uint64_t(s) * 4096);
        }
        char name[32];
        snprintf(name, sizeof(name), "segs_%d", variant);
        put("fuzz_file_seg_data", name, writer.data().to_string());
    }
}

static void log_segment_seeds() {
    for (int variant = 0; variant < 3; ++variant) {
        harness::ScratchDir dir;
        std::string bytes;
        int count = 0;
        {
            braft::SegmentLogStorage storage(dir.path, false);
            braft::ConfigurationManager conf_manager;
            if (storage.init(&conf_manager) != 0) {
                fprintf(stderr, "init failed\n");
                return;
            }
            const int entries = 1 + variant * 3;
            for (int i = 1; i <= entries; ++i) {
                braft::LogEntry* e = new braft::LogEntry();
                e->AddRef();
                e->id = braft::LogId(i, 1 + variant);
                if (variant == 2 && i == 1) {
                    e->type = braft::ENTRY_TYPE_CONFIGURATION;
                    e->peers = new std::vector<braft::PeerId>;
                    e->peers->push_back(braft::PeerId("127.0.0.1:8100:0"));
                    e->peers->push_back(braft::PeerId("127.0.0.1:8101:0"));
                } else if (variant == 1 && i == 2) {
                    e->type = braft::ENTRY_TYPE_NO_OP;
                } else {
                    e->type = braft::ENTRY_TYPE_DATA;
                    e->data.append(std::string(8 + i * 5, char('a' + i)));
                }
                if (storage.append_entry(e) != 0) {
                    fprintf(stderr, "append failed\n");
                }
                e->Release();
                ++count;
            }
        }
        // the open segment the storage left behind
        DIR* d = opendir(dir.path.c_str());
        std::string file;
        while (struct dirent* de = readdir(d)) {
            if (strncmp(de->d_name, "log_inprogress_", 15) == 0) file = de->d_name;
        }
        closedir(d);
        bytes = slurp(dir.path + "/" + file);
        char name[32];
        snprintf(name, sizeof(name), "open_%d", variant);
        put("fuzz_log_segment", name, std::string(1, '\0') + std::string(1, '\0') + bytes);
        snprintf(name, sizeof(name), "closed_%d", variant);
        put("fuzz_log_segment", name, std::string(1, '\1') + std::string(1, char(count - 1)) + bytes);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <mayhem-dir>\n", argv[0]);
        return 2;
    }
    g_root = argv[1];
    harness::quiet_logging();
    configuration_seeds();
    raft_meta_seeds();
    snapshot_meta_seeds();
    file_seg_seeds();
    log_segment_seeds();
    return 0;
}
