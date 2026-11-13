// Shared helpers for the braft fuzz harnesses: a per-input scratch directory under $TMPDIR.
#ifndef BRAFT_MAYHEM_HARNESS_UTIL_H
#define BRAFT_MAYHEM_HARNESS_UTIL_H

#include <ftw.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

#include <butil/logging.h>

namespace harness {

// libFuzzer calls this once; keeps braft's per-input INFO/ERROR logging off stderr.
// FATAL (CHECK failures) still prints and aborts.
inline void quiet_logging() { ::logging::SetMinLogLevel(::logging::BLOG_FATAL); }

inline std::string make_scratch_dir() {
    const char* tmp = getenv("TMPDIR");
    std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") + "/braft-fuzz-XXXXXX";
    std::string buf(tmpl);
    if (mkdtemp(&buf[0]) == NULL) {
        abort();
    }
    return buf;
}

inline int unlink_cb(const char* path, const struct stat*, int, struct FTW*) {
    return remove(path);
}

inline void remove_tree(const std::string& dir) {
    nftw(dir.c_str(), unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
}

inline bool write_file(const std::string& path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    return (fclose(f) == 0) && ok;
}

struct ScratchDir {
    std::string path;
    ScratchDir() : path(make_scratch_dir()) {}
    ~ScratchDir() { remove_tree(path); }
};

}  // namespace harness

#endif
