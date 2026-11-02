#include "platform/saves_io.h"

#include <windows.h>

namespace bs::native::saves::io {

namespace fs = std::filesystem;

bool is_read_only(const fs::path& p) {
    const DWORD at = GetFileAttributesW(p.c_str());
    return at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_READONLY);
}

SaveError write_durable(const fs::path& p, const std::vector<std::byte>& data, size_t n, bool fail_flush) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_ACCESS_DENIED ? SaveError::ReadOnly : SaveError::Io;
    DWORD wrote = 0;
    bool  ok    = WriteFile(h, data.data(), DWORD(n), &wrote, nullptr) && wrote == n && n == data.size();
    if (ok && (fail_flush || !FlushFileBuffers(h))) ok = false;
    CloseHandle(h);
    return ok ? SaveError::None : SaveError::Io;
}

bool move_new(const fs::path& tmp, const fs::path& dst) { return MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_WRITE_THROUGH) != 0; }

bool replace_keep_backup(const fs::path& dst, const fs::path& tmp, const fs::path& bak) {
    if (ReplaceFileW(dst.c_str(), tmp.c_str(), bak.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
    // ERROR_UNABLE_TO_MOVE_REPLACEMENT_2: o destino ja virou .bak e o temporario nao entrou
    std::error_code ec;
    if (!fs::exists(dst, ec) && fs::exists(bak, ec)) return move_new(tmp, dst);
    return false;
}

bool move_replace(const fs::path& tmp, const fs::path& dst) {
    return MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

} // namespace bs::native::saves::io
