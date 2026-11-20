// bs-extract: le a imagem de disco do usuario e extrai IP.BIN, 1ST_READ.BIN
// e os arquivos de dados para game-data/, com manifest.json de hashes.
//   --runtime: prepara os dados de execucao do jogo nativo (runtime_data.h) e confere
//              todos os setores contra a imagem.
#include "disc/archive.h"
#include "disc/disc_open.h"
#include "disc/gdi_export.h"
#include "extract.h"
#include "runtime_data.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

static void usage() {
    std::fprintf(stderr,
                 "uso: bs-extract [--info] [--force] <imagem .gdi|.cue|.chd|.cdi|.zip|.7z> [saida]\n"
                 "     bs-extract --runtime <imagem .gdi|.cue|.chd|.cdi|.zip|.7z> <pasta de dados>\n"
                 "     bs-extract --gdi <imagem> <pasta>: a imagem como GDI (setores brutos; patcher do Director's Cut)\n"
                 "  saida      pasta de destino (padrao: game-data)\n"
                 "  --info     so mostra faixas, IP.BIN e hash do executavel\n"
                 "  --force    extrai mesmo com hash de executavel desconhecido\n"
                 "  --runtime  dados de execucao do jogo nativo (o jogo nao le a imagem)\n");
}

static int runRuntime(const std::string& image, const std::string& out, const std::filesystem::path& confine) {
    using clock = std::chrono::steady_clock;
    const auto           t0 = clock::now();
    const auto           img = bs::disc::disc_open(image, confine);
    const auto           r  = bs::disc::prepare_runtime_data(*img, out);
    const auto           t1 = clock::now();
    std::cout << "dados de execucao: " << r.files << " arquivos, " << r.bytes / (1024 * 1024) << " MB, "
              << r.metaSectors << " setores de sistema (" << r.versionId << ") em "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms\n";
    const std::string diff = bs::disc::verify_runtime_data(*img, out);
    const auto        t2   = clock::now();
    std::cout << "conferencia setor a setor: " << (diff.empty() ? "identico a imagem" : diff) << " ("
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << " ms)\n";
    return diff.empty() ? 0 : 1;
}

// .zip/.7z (I8): a imagem e extraida para <saida>.imagem, apagada no fim
struct Unpacked {
    std::filesystem::path dir;
    ~Unpacked() {
        std::error_code ec;
        if (!dir.empty()) std::filesystem::remove_all(dir, ec);
    }
};
static std::string unpack(const std::string& image, const std::string& out, Unpacked& u) {
#if BS_DISC_CHD
    if (!bs::disc::archive_supported(image)) return image;
    u.dir = std::filesystem::path(out).concat(".imagem");
    std::filesystem::remove_all(u.dir);
    std::cout << "extraindo o pacote para " << u.dir.string() << "\n";
    return bs::disc::archive_extract_image(image, u.dir).string();
#else
    // BS_BUILD_TOOLS=OFF: sem a libchdr, o miniz e o LZMA SDK; o .zip, o .7z e o .chd sao recusados por disc_open, com a
    // mensagem (o .cdi, o .gdi e o .cue abrem)
    (void)out, (void)u;
    return image;
#endif
}

int main(int argc, char** argv) {
    bs::ExtractOptions opt;
    opt.out = "game-data";
    int positional = 0;
    if (argc == 4 && std::string(argv[1]) == "--gdi") {  // Director's Cut: a imagem como GDI (gdi_export.h)
        try {
            Unpacked u;
            const std::string img = unpack(argv[2], argv[3], u);
            const auto        d   = bs::disc::disc_open(img, u.dir);
            const auto        gdi = bs::disc::export_gdi(*d, argv[3], "disco");
            std::cout << "GDI: " << gdi.string() << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "erro: " << e.what() << "\n";
            return 1;
        }
    }
    if (argc == 4 && std::string(argv[1]) == "--runtime") {
        try {
            Unpacked u;
            const std::string img = unpack(argv[2], argv[3], u);
            return runRuntime(img, argv[3], u.dir);
        } catch (const std::exception& e) {
            std::cerr << "erro: " << e.what() << "\n";
            return 1;
        }
    }
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--info") opt.info_only = true;
        else if (a == "--force") opt.force = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else if (positional == 0) { opt.image = a; ++positional; }
        else if (positional == 1) { opt.out = a; ++positional; }
        else { usage(); return 2; }
    }
    if (positional == 0) { usage(); return 2; }
    try {
        Unpacked u;
        opt.image   = unpack(opt.image.string(), opt.out.string(), u);
        opt.confine = u.dir;
        return bs::runExtract(opt, std::cout);
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
