#pragma once
// UTF-8 para wstring, igual nos dois sistemas: UTF-16 onde wchar_t tem 2 bytes (Windows), UTF-32 onde tem 4 (Linux).
#include <cstdint>
#include <string>
#include <string_view>

namespace bs::native {

inline std::wstring utf8_to_wide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const uint8_t c    = uint8_t(s[i]);
        const size_t  more = c < 0x80 ? 0 : c >> 5 == 6 ? 1 : c >> 4 == 14 ? 2 : c >> 3 == 30 ? 3 : size_t(-1);
        bool          ok   = more != size_t(-1) && i + (more == size_t(-1) ? 0 : more) < s.size();
        uint32_t      cp   = 0xFFFD;
        if (ok) {
            cp = more == 0 ? c : c & (0x3Fu >> more);
            for (size_t k = 1; k <= more; ++k) {
                const uint8_t b = uint8_t(s[i + k]);
                if (b >> 6 != 2) {
                    ok = false, cp = 0xFFFD;
                    break;
                }
                cp = cp << 6 | (b & 0x3F);
            }
        }
        i += ok ? more + 1 : 1;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out += wchar_t(0xD800 + (cp >> 10)), out += wchar_t(0xDC00 + (cp & 0x3FF));
                continue;
            }
        }
        out += wchar_t(cp);
    }
    return out;
}

} // namespace bs::native
