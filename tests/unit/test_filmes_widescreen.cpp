// v0.9.7-2: o mod "filmes widescreen" (builder/filmes_widescreen.h) com um download falso: lista, hash, arquivo do jogador,
// atualizacao do que instalamos, falha de rede e cancelamento. Sem rede e sem dados do jogo.
#include "test.h"

#include "filmes_widescreen.h"
#include "util/hash.h"

#include <atomic>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder::widescreen;

namespace {

struct Pasta {
    fs::path p;
    explicit Pasta(const char* nome) {
        p = fs::temp_directory_path() / (std::string("bs unit widescreen ") + nome);
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p);
    }
    ~Pasta() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void grava(const fs::path& f, const std::string& s) { std::ofstream(f, std::ios::binary | std::ios::trunc) << s; }
std::string le(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
std::string sha(const std::string& s) { return bs::util::sha256Hex(s.data(), s.size()); }

// servidor falso: url -> conteudo; conta os pedidos
struct Servidor {
    std::map<std::string, std::string> arquivos;
    std::map<std::string, int>         pedidos;
    bool                               semRede = false;
    Fetch fetch() {
        return [this](const std::string& url, const fs::path& dst, const std::atomic<bool>* cancel,
                      const std::function<void(uint64_t, uint64_t)>& on, std::string* err) {
            ++pedidos[url];
            if (semRede || (cancel && cancel->load())) {
                *err = semRede ? "sem rede" : "cancelado";
                return false;
            }
            const auto it = arquivos.find(url);
            if (it == arquivos.end()) {
                *err = "HTTP 404";
                return false;
            }
            grava(dst, it->second);
            if (on) on(it->second.size(), it->second.size());
            return true;
        };
    }
};

const std::string kBase = "https://exemplo.invalid/item/";

} // namespace

BS_TEST(widescreen_parse_sums_aceita_so_nomes_e_hashes_seguros) {
    const std::string h = std::string(64, 'a');
    const auto l = parse_sums(h + "  E001.mp4\r\n" + h + "  *NEWGAME.mp4\n" + h + "  ../x.mp4\n" + h + "  a b.mp4\n" + "zz  E002.mp4\n" +
                              std::string(64, 'A') + "  E003.mp4\n" + h + "  E001.mp4\n" + h + "  manifest.json\n");
    BS_CHECK(l.size() == 2);
    BS_CHECK(l[0].file == "E001.mp4" && l[0].sha256 == h);
    BS_CHECK(l[1].file == "NEWGAME.mp4");
    BS_CHECK(!safe_name("..mp4") && !safe_name("E001.MP4.exe") && safe_name("E185A.mp4"));
}

BS_TEST(widescreen_baixa_confere_e_registra) {
    Pasta    d("baixa");
    Servidor s;
    s.arquivos[kBase + "E001.mp4"] = "filme 1";
    s.arquivos[kBase + "E002.mp4"] = "filme 2";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme 1") + "  E001.mp4\n" + sha("filme 2") + "  E002.mp4\n";
    const Report r = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase);
    BS_CHECK(r.baixados == 2 && r.falhas == 0 && r.aviso.empty());
    BS_CHECK(le(d.p / "E001.mp4") == "filme 1" && le(d.p / "E002.mp4") == "filme 2");
    BS_CHECK(!fs::exists(d.p / "E001.mp4.part"));
    BS_CHECK(read_registry(d.p).size() == 2);
    // de novo: nada baixado (ja presentes e conferidos)
    const Report r2 = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase);
    BS_CHECK(r2.baixados == 0 && r2.presentes == 2);
    BS_CHECK(s.pedidos[kBase + "E001.mp4"] == 1);
}

// v0.9.7-4: o andamento que o instalador mostra (filme i de n, bytes do filme, total da sessao) e o fim, uma vez
BS_TEST(widescreen_andamento_por_filme_e_fim) {
    Pasta    d("andamento");
    Servidor s;
    s.arquivos[kBase + "E001.mp4"] = "filme 1";
    s.arquivos[kBase + "E002.mp4"] = "filme dois";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme 1") + "  E001.mp4\n" + sha("filme dois") + "  E002.mp4\n";
    std::vector<Andamento> v;
    const Report r = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase, [&](const Andamento& a) { v.push_back(a); });
    BS_CHECK(r.baixados == 2);
    BS_CHECK(v.size() == 5);  // E001: inicio e bytes; E002: inicio e bytes; fim
    BS_CHECK(v[0].atual == 1 && v[0].quantos == 2 && v[0].arquivo == "E001.mp4" && v[0].recebidos == 0 && !v[0].fim);
    BS_CHECK(v[1].recebidos == 7 && v[1].tamanho == 7 && v[1].sessao == 7);
    BS_CHECK(v[2].atual == 2 && v[2].arquivo == "E002.mp4" && v[2].sessao == 7);
    BS_CHECK(v[3].recebidos == 10 && v[3].sessao == 17);
    BS_CHECK(v[4].fim && v[4].sessao == 17);
    // ja presentes: nada a baixar, so o fim
    v.clear();
    install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase, [&](const Andamento& a) { v.push_back(a); });
    BS_CHECK(v.size() == 1 && v[0].fim);
}

BS_TEST(widescreen_hash_errado_e_descartado) {
    Pasta    d("hash");
    Servidor s;
    s.arquivos[kBase + "E001.mp4"] = "corrompido";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme 1") + "  E001.mp4\n";
    const Report r = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase);
    BS_CHECK(r.baixados == 0 && r.falhas == 1 && !r.aviso.empty());
    BS_CHECK(!fs::exists(d.p / "E001.mp4") && !fs::exists(d.p / "E001.mp4.part"));
}

BS_TEST(widescreen_nao_troca_filme_do_jogador_mas_atualiza_o_nosso) {
    Pasta    d("jogador");
    Servidor s;
    grava(d.p / "E001.mp4", "mod do jogador");
    grava(d.p / "E002.mp4", "versao antiga nossa");
    grava(d.p / ".filmes-widescreen", sha("versao antiga nossa") + "  E002.mp4\n");
    s.arquivos[kBase + "E001.mp4"] = "filme 1";
    s.arquivos[kBase + "E002.mp4"] = "filme 2 novo";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme 1") + "  E001.mp4\n" + sha("filme 2 novo") + "  E002.mp4\n";
    const Report r = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase);
    BS_CHECK(r.doJogador == 1 && r.baixados == 1);
    BS_CHECK(le(d.p / "E001.mp4") == "mod do jogador");
    BS_CHECK(le(d.p / "E002.mp4") == "filme 2 novo");
}

BS_TEST(widescreen_sem_rede_e_cancelado_viram_aviso) {
    Pasta    d("rede");
    Servidor s;
    s.semRede = true;
    const Report r = install(d.p, s.fetch(), nullptr, nullptr, nullptr, kBase);
    BS_CHECK(r.falhas == 1 && !r.cancelado && !r.aviso.empty());
    s.semRede = false;
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("x") + "  E001.mp4\n";
    s.arquivos[kBase + "E001.mp4"] = "x";
    std::atomic<bool> cancelar{true};
    const Report r2 = install(d.p, s.fetch(), &cancelar, nullptr, nullptr, kBase);
    BS_CHECK(r2.cancelado && r2.baixados == 0 && !fs::exists(d.p / "E001.mp4"));
}

// 2026-11-24 (mantenedor: "Quando instalar uma atualizacao, nao deve precisar baixar os videos de novo"): o cache compartilhado
BS_TEST(widescreen_cache_evita_baixar_de_novo_em_outra_pasta) {
    Pasta    a("cache a"), b("cache b"), cache("cache c");
    Servidor s;
    s.arquivos[kBase + "E001.mp4"] = "filme 1";
    s.arquivos[kBase + "E002.mp4"] = "filme 2";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme 1") + "  E001.mp4\n" + sha("filme 2") + "  E002.mp4\n";
    const Report r1 = install(a.p, s.fetch(), nullptr, nullptr, nullptr, kBase, nullptr, cache.p);
    BS_CHECK(r1.baixados == 2 && r1.doCache == 0);
    BS_CHECK(le(cache.p / "E001.mp4") == "filme 1" && le(cache.p / "E002.mp4") == "filme 2");
    // outra pasta de instalacao (ou uma reinstalacao do zero): os filmes vem do cache, nenhum download de filme
    const Report r2 = install(b.p, s.fetch(), nullptr, nullptr, nullptr, kBase, nullptr, cache.p);
    BS_CHECK(r2.baixados == 0 && r2.doCache == 2 && r2.falhas == 0 && r2.aviso.empty());
    BS_CHECK(le(b.p / "E001.mp4") == "filme 1" && le(b.p / "E002.mp4") == "filme 2");
    BS_CHECK(read_registry(b.p).size() == 2);
    BS_CHECK(s.pedidos[kBase + "E001.mp4"] == 1 && s.pedidos[kBase + "E002.mp4"] == 1);
}

BS_TEST(widescreen_cache_com_conteudo_errado_baixa_e_corrige) {
    Pasta    a("cache errado a"), cache("cache errado c");
    Servidor s;
    s.arquivos[kBase + "E001.mp4"] = "filme novo";
    s.arquivos[kBase + "SHA256SUMS.txt"] = sha("filme novo") + "  E001.mp4\n";
    grava(cache.p / "E001.mp4", "filme velho");  // versao antiga no cache: nao pode entrar
    const Report r = install(a.p, s.fetch(), nullptr, nullptr, nullptr, kBase, nullptr, cache.p);
    BS_CHECK(r.baixados == 1 && r.doCache == 0);
    BS_CHECK(le(a.p / "E001.mp4") == "filme novo");
    BS_CHECK(le(cache.p / "E001.mp4") == "filme novo");  // o cache foi atualizado com o filme conferido
}
