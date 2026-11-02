// N9-L: os mesmos passos de saves_io_win.cpp. rename() e atomico; fsync do arquivo e da pasta fazem o papel do write-through.
#include "platform/saves_io.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bs::native::saves::io {

namespace fs = std::filesystem;

namespace {

bool syncDir(const fs::path& file) {
    const int d = ::open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (d < 0) return false;
    const bool ok = fsync(d) == 0 || errno == EINVAL;  // alguns sistemas de arquivos nao sincronizam pastas
    ::close(d);
    return ok;
}

bool renameDurable(const fs::path& from, const fs::path& to) { return std::rename(from.c_str(), to.c_str()) == 0 && syncDir(to); }

} // namespace

bool is_read_only(const fs::path& p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0 && !(st.st_mode & S_IWUSR);
}

SaveError write_durable(const fs::path& p, const std::vector<std::byte>& data, size_t n, bool fail_flush) {
    const int f = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (f < 0) return errno == EACCES || errno == EROFS ? SaveError::ReadOnly : SaveError::Io;
    size_t done = 0;
    while (done < n) {
        const ssize_t w = ::write(f, data.data() + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) break;
        done += size_t(w);
    }
    bool ok = done == n && n == data.size();
    if (ok && (fail_flush || fsync(f) != 0)) ok = false;
    ::close(f);
    return ok ? SaveError::None : SaveError::Io;
}

bool move_new(const fs::path& tmp, const fs::path& dst) { return renameDurable(tmp, dst); }

bool replace_keep_backup(const fs::path& dst, const fs::path& tmp, const fs::path& bak) {
    // com link, dst nunca deixa de existir. Sistemas de arquivos sem link (exFAT em cartao) passam por um instante sem dst, e a
    // recuperacao de saves_store.cpp traz o .bak de volta
    if (link(dst.c_str(), bak.c_str()) != 0 && std::rename(dst.c_str(), bak.c_str()) != 0) return false;
    return renameDurable(tmp, dst);
}

bool move_replace(const fs::path& tmp, const fs::path& dst) { return renameDurable(tmp, dst); }

} // namespace bs::native::saves::io
