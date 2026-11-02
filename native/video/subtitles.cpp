#include "video/subtitles.h"

#include "core/utf8.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bs::native::video {

namespace {

// "00:01:02,345" (SRT) ou "01:02.345" / "00:01:02.345" (VTT) em 100 ns; -1 se invalido
int64_t parseTime(std::string_view t) {
    int    parts[4] = {0, 0, 0, 0}, n = 0, digits = 0;
    size_t i = 0;
    for (; i < t.size() && n < 4; ++i) {
        const char c = t[i];
        if (c >= '0' && c <= '9') parts[n] = parts[n] * 10 + (c - '0'), ++digits;
        else if (c == ':' || c == ',' || c == '.') {
            if (!digits) return -1;
            ++n, digits = 0;
        } else break;
    }
    if (digits) ++n;
    int64_t h = 0, m = 0, s = 0, ms = 0;
    if (n == 4) h = parts[0], m = parts[1], s = parts[2], ms = parts[3];
    else if (n == 3) m = parts[0], s = parts[1], ms = parts[2];
    else return -1;
    if (m > 59 || s > 59 || ms > 999) return -1;
    return ((h * 3600 + m * 60 + s) * 1000 + ms) * 10'000;
}

std::string stripTags(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {  // <i>, </i>, <b>, <font ...>, <c.classe>
            const size_t e = s.find('>', i);
            if (e != std::string::npos) { i = e; continue; }
        }
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') {  // {\an8} do SRT
            const size_t e = s.find('}', i);
            if (e != std::string::npos) { i = e; continue; }
        }
        out += s[i];
    }
    return out;
}

} // namespace

bool parse_subtitles(std::string_view in, std::vector<SubtitleCue>& cues, std::vector<std::string>* warnings) {
    cues.clear();
    std::string text(in);
    if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF) text.erase(0, 3);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::istringstream ss(text);
    std::string        line;
    int                lineNo = 0;
    while (std::getline(ss, line)) {
        ++lineNo;
        const size_t arrow = line.find("-->");
        if (arrow == std::string::npos) continue;  // indices do SRT, WEBVTT, NOTE, linhas soltas
        std::string a = line.substr(0, arrow), b = line.substr(arrow + 3);
        auto trim = [](std::string& s) {
            const size_t p = s.find_first_not_of(" \t");
            s = p == std::string::npos ? std::string() : s.substr(p);
        };
        trim(a), trim(b);
        SubtitleCue c;
        c.start100ns = parseTime(a);
        c.end100ns   = parseTime(b);  // o VTT pode ter configuracoes depois do tempo: parseTime para nelas
        std::string body;
        while (std::getline(ss, line) && !line.empty()) {
            ++lineNo;
            if (!body.empty()) body += '\n';
            body += stripTags(line);
        }
        ++lineNo;
        if (c.start100ns < 0 || c.end100ns < 0 || c.end100ns <= c.start100ns) {
            if (warnings) warnings->push_back("legenda: tempo invalido perto da linha " + std::to_string(lineNo));
            continue;
        }
        c.text = utf8_to_wide(body);
        cues.push_back(std::move(c));
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& x, const SubtitleCue& y) { return x.start100ns < y.start100ns; });
    return true;
}

bool load_subtitles(const std::filesystem::path& file, std::vector<SubtitleCue>& cues, std::string* err, std::vector<std::string>* warnings) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "legenda nao encontrada: " + file.filename().string();
        return false;
    }
    std::string s((std::istreambuf_iterator<char>(f)), {});
    return parse_subtitles(s, cues, warnings);
}

std::wstring subtitle_at(const std::vector<SubtitleCue>& cues, int64_t t) {
    std::wstring out;
    for (const auto& c : cues) {
        if (c.start100ns > t) break;
        if (t < c.end100ns) out += (out.empty() ? L"" : L"\n") + c.text;
    }
    return out;
}

} // namespace bs::native::video
