#include "disc/track_files_image.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace bs::disc {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// separa por espacos; "entre aspas" vira um token so (nomes de arquivo com espacos)
std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    size_t                   i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size()) break;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            if (end == std::string::npos) throw DiscError("aspas sem fechamento: " + line);
            out.push_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            size_t end = i;
            while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) ++end;
            out.push_back(line.substr(i, end - i));
            i = end;
        }
    }
    return out;
}

uint32_t number(const std::string& s, const std::string& line) {
    if (s.empty() || s.size() > 9 || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
        throw DiscError("numero invalido '" + s + "' em: " + line);
    return uint32_t(std::stoul(s));
}

// mm:ss:ff (75 quadros por segundo) em setores
uint32_t msf(const std::string& s, const std::string& line) {
    unsigned m = 0, sec = 0, f = 0;
    char     c1 = 0, c2 = 0;
    std::istringstream in(s);
    if (!(in >> m >> c1 >> sec >> c2 >> f) || c1 != ':' || c2 != ':' || sec >= 60 || f >= 75)
        throw DiscError("tempo invalido '" + s + "' em: " + line);
    return (m * 60 + sec) * 75 + f;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream       in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (out.empty() && l.size() >= 3 && uint8_t(l[0]) == 0xEF && uint8_t(l[1]) == 0xBB && uint8_t(l[2]) == 0xBF) l.erase(0, 3);
        out.push_back(l);
    }
    return out;
}

void checkTracks(const std::vector<Track>& t) {
    if (t.empty()) throw DiscError("a imagem nao tem faixas");
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].number != int(i + 1)) throw DiscError("faixas fora de ordem (faixa " + std::to_string(t[i].number) + ")");
        if (!t[i].length) throw DiscError("faixa " + std::to_string(t[i].number) + " vazia");
        if (i && t[i].start_lba < t[i - 1].start_lba + t[i - 1].length)
            throw DiscError("faixa " + std::to_string(t[i].number) + " sobrepoe a anterior");
    }
}

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw DiscError("nao foi possivel abrir " + pathText(p));
    std::ostringstream s;
    s << f.rdbuf();
    std::string t = s.str();
    if (t.size() > (1u << 20)) throw DiscError(pathText(p) + " grande demais para uma folha de faixas");
    return t;
}

} // namespace

std::vector<Track> TrackFilesImage::parseGdi(const std::string& text, const SizeFn& fileSize, std::vector<std::string>& files) {
    std::vector<Track> out;
    uint32_t           count = 0;
    bool               head  = true;
    for (const std::string& line : lines(text)) {
        const auto tk = tokens(line);
        if (tk.empty()) continue;
        if (head) {
            if (tk.size() != 1) throw DiscError("GDI: a primeira linha deve ter o numero de faixas: " + line);
            count = number(tk[0], line);
            if (!count || count > 99) throw DiscError("GDI: numero de faixas invalido: " + line);
            head = false;
            continue;
        }
        // numero  LBA  tipo(4 dados, 0 audio)  tamanho do setor  arquivo  deslocamento
        if (tk.size() != 6) throw DiscError("GDI: linha de faixa invalida: " + line);
        Track t;
        t.number      = int(number(tk[0], line));
        t.start_lba   = number(tk[1], line);
        const uint32_t type = number(tk[2], line);
        t.sector_size = number(tk[3], line);
        if (number(tk[5], line) != 0) throw DiscError("GDI: deslocamento diferente de 0 nao suportado: " + line);
        if (type == 4) t.mode = TrackMode::Mode1;
        else if (type == 0) t.mode = TrackMode::Audio;
        else throw DiscError("GDI: tipo de faixa desconhecido: " + line);
        if (t.sector_size != 2352 && t.sector_size != 2048) throw DiscError("GDI: tamanho de setor invalido: " + line);
        if (t.mode == TrackMode::Audio && t.sector_size != 2352) throw DiscError("GDI: faixa de audio sem 2352: " + line);
        const uint64_t sz = fileSize(tk[4]);
        if (t.isData() && sz % t.sector_size) throw DiscError("GDI: " + tk[4] + " nao tem setores inteiros");
        if (sz / t.sector_size > 0xFFFFFFFFull) throw DiscError("GDI: " + tk[4] + " grande demais");
        t.length     = uint32_t(sz / t.sector_size);
        t.session    = t.start_lba >= kGdHighDensityLba ? 2 : 1;
        t.file_index = int(files.size());
        files.push_back(tk[4]);
        out.push_back(t);
    }
    if (head) throw DiscError("GDI vazio");
    if (out.size() != count) throw DiscError("GDI: anuncia " + std::to_string(count) + " faixas e lista " + std::to_string(out.size()));
    checkTracks(out);
    return out;
}

std::vector<Track> TrackFilesImage::parseCue(const std::string& text, const SizeFn& fileSize, std::vector<std::string>& files) {
    struct Pending {
        Track    t;
        uint32_t idx0 = 0, idx1 = 0;
        bool     has0 = false, has1 = false;
        uint32_t extraPregap = 0;  // PREGAP: setores que nao estao no arquivo
    };
    std::vector<Track>   out;
    std::vector<Pending> inFile;  // faixas do arquivo atual
    uint32_t             cursor = 0, fileStart = 0, fileSectors = 0;
    int                  session = 1;
    bool                 haveFile = false;

    auto closeFile = [&]() {
        // PREGAP (so na 1a faixa do arquivo): setores virtuais antes do arquivo, deslocam todas as faixas dele
        const uint32_t shift = inFile.empty() ? 0 : inFile.front().extraPregap;
        for (size_t i = 0; i < inFile.size(); ++i) {
            Pending& p = inFile[i];
            if (!p.has1) throw DiscError("CUE: faixa " + std::to_string(p.t.number) + " sem INDEX 01");
            const uint32_t endInFile = i + 1 < inFile.size() ? (inFile[i + 1].has0 ? inFile[i + 1].idx0 : inFile[i + 1].idx1)
                                                             : fileSectors;
            if (p.idx1 > endInFile || (p.has0 && p.idx0 > p.idx1)) throw DiscError("CUE: indices fora de ordem na faixa " +
                                                                                   std::to_string(p.t.number));
            p.t.start_lba   = fileStart + shift + p.idx1;
            p.t.pregap      = (p.has0 ? p.idx1 - p.idx0 : 0) + (i == 0 ? shift : 0);
            p.t.file_offset = uint64_t(p.idx1) * p.t.sector_size;
            p.t.length      = endInFile - p.idx1;
            out.push_back(p.t);
        }
        cursor = fileStart + shift + fileSectors;
        inFile.clear();
    };

    for (const std::string& line : lines(text)) {
        const auto tk = tokens(line);
        if (tk.empty()) continue;
        const std::string cmd = lower(tk[0]);
        if (cmd == "rem") {
            const std::string rest = lower(line);
            if (rest.find("high-density area") != std::string::npos) {
                if (haveFile) closeFile();
                haveFile = false;
                if (cursor > kGdHighDensityLba) throw DiscError("CUE: a area de baixa densidade passa da LBA 45000");
                cursor  = kGdHighDensityLba;
                session = 2;
            }
            continue;
        }
        if (cmd == "file") {
            if (tk.size() < 2) throw DiscError("CUE: FILE sem nome: " + line);
            if (tk.size() >= 3 && lower(tk.back()) != "binary") throw DiscError("CUE: so arquivos BINARY: " + line);
            if (haveFile) closeFile();
            haveFile  = true;
            fileStart = cursor;
            files.push_back(tk[1]);
            fileSectors = 0;  // definido na primeira faixa (pelo tamanho do setor)
            continue;
        }
        if (cmd == "track") {
            if (!haveFile || tk.size() != 3) throw DiscError("CUE: TRACK sem FILE ou mal formado: " + line);
            Pending p;
            p.t.number = int(number(tk[1], line));
            const std::string m = lower(tk[2]);
            if (m == "mode1/2352") p.t.mode = TrackMode::Mode1, p.t.sector_size = 2352;
            else if (m == "mode1/2048") p.t.mode = TrackMode::Mode1, p.t.sector_size = 2048;
            else if (m == "mode2/2352") p.t.mode = TrackMode::Mode2, p.t.sector_size = 2352;
            else if (m == "mode2/2336") p.t.mode = TrackMode::Mode2, p.t.sector_size = 2336;
            else if (m == "audio") p.t.mode = TrackMode::Audio, p.t.sector_size = 2352;
            else throw DiscError("CUE: modo de faixa nao suportado: " + line);
            p.t.session    = session;
            p.t.file_index = int(files.size()) - 1;
            if (!inFile.empty() && inFile.front().t.sector_size != p.t.sector_size)
                throw DiscError("CUE: faixas com setores de tamanhos diferentes no mesmo arquivo: " + line);
            if (inFile.empty()) {
                const uint64_t sz = fileSize(files.back());
                if (p.t.isData() && sz % p.t.sector_size) throw DiscError("CUE: " + files.back() + " nao tem setores inteiros");
                if (sz / p.t.sector_size > 0xFFFFFFFFull) throw DiscError("CUE: " + files.back() + " grande demais");
                fileSectors = uint32_t(sz / p.t.sector_size);
            }
            inFile.push_back(p);
            continue;
        }
        if (cmd == "index") {
            if (inFile.empty() || tk.size() != 3) throw DiscError("CUE: INDEX fora de faixa: " + line);
            const uint32_t n = number(tk[1], line), v = msf(tk[2], line);
            if (n == 0) inFile.back().idx0 = v, inFile.back().has0 = true;
            else if (n == 1) inFile.back().idx1 = v, inFile.back().has1 = true;
            continue;  // indices 02+ nao mudam o inicio da faixa
        }
        if (cmd == "pregap") {
            if (inFile.empty() || tk.size() != 2 || inFile.size() > 1)
                throw DiscError("CUE: PREGAP so e suportado na primeira faixa de um arquivo: " + line);
            inFile.back().extraPregap = msf(tk[1], line);
            continue;
        }
        if (cmd == "postgap") throw DiscError("CUE: POSTGAP nao suportado: " + line);
        // CATALOG, TITLE, PERFORMER, FLAGS, ISRC, SONGWRITER: sem efeito no layout
    }
    if (haveFile) closeFile();
    checkTracks(out);
    return out;
}

std::unique_ptr<TrackFilesImage> TrackFilesImage::open(const fs::path& p, bool cue, const fs::path& confine) {
    std::unique_ptr<TrackFilesImage> img(new TrackFilesImage());
    const std::string                text = readText(p);
    const fs::path                   dir  = p.parent_path();
    auto resolve = [&](const std::string& name) {
        const fs::path rel = u8(name);
        if (!confine.empty()) {
            bool up = false;
            for (const auto& part : rel) up = up || part == "..";
            std::error_code e1, e2;
            const fs::path  root = fs::weakly_canonical(confine, e1), got = fs::weakly_canonical(dir / rel, e2);
            const fs::path  rr   = got.lexically_relative(root);
            if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory() || up || e1 || e2 || rr.empty() ||
                *rr.begin() == "..")
                throw DiscError("a folha " + pathText(p.filename()) + " cita uma faixa fora do pacote: " + name);
            return dir / rel;
        }
        if (rel.is_absolute() || rel.has_root_name()) return rel;
        return dir / rel;
    };
    auto size = [&](const std::string& name) -> uint64_t {
        std::error_code ec;
        const auto      s = fs::file_size(resolve(name), ec);
        if (ec) throw DiscError("faixa nao encontrada: " + pathText(resolve(name)) + " (citada em " + pathText(p.filename()) + ")");
        return s;
    };
    std::vector<std::string> names;
    img->tracks_ = cue ? parseCue(text, size, names) : parseGdi(text, size, names);
    for (const auto& n : names) img->files_.push_back(std::make_unique<InputFile>(resolve(n)));
    img->format_ = cue ? "CUE/BIN" : "GDI";
    return img;
}

std::unique_ptr<TrackFilesImage> TrackFilesImage::openGdi(const fs::path& gdi, const fs::path& confine) {
    return open(gdi, false, confine);
}
std::unique_ptr<TrackFilesImage> TrackFilesImage::openCue(const fs::path& cue, const fs::path& confine) {
    return open(cue, true, confine);
}

void TrackFilesImage::readRaw(const Track& t, uint64_t offset, void* dst, size_t n) const {
    if (t.file_index < 0 || size_t(t.file_index) >= files_.size()) throw DiscError("faixa sem arquivo");
    files_[size_t(t.file_index)]->read(offset, dst, n);
}

} // namespace bs::disc
