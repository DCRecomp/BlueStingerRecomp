// v0.9.7-5: o compilador portatil do Windows (builder/compilador_portatil.h) com pecas falsas no mesmo formato dos zips
// oficiais e um download falso: montagem, a arvore inteira conferida pelo hash esperado (executavel, bibliotecas, modulos do
// CMake, licencas, arquivo a mais ou a menos), hash errado recusado sem nada no lugar, a anterior mantida quando a nova nao
// confere, cancelamento e nomes inseguros. Sem rede.
#include "test.h"

#include "compilador_portatil.h"
#include "util/hash.h"

#include <miniz.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace bs::builder;

namespace {

struct Pasta {
    fs::path p;
    explicit Pasta(const char* nome) {
        p = fs::temp_directory_path() / (std::string("bs unit compilador ") + nome);
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p);
    }
    ~Pasta() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

std::string zip(const std::vector<std::pair<std::string, std::string>>& arquivos) {
    mz_zip_archive z{};
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& [n, c] : arquivos) mz_zip_writer_add_mem(&z, n.c_str(), c.data(), c.size(), MZ_DEFAULT_COMPRESSION);
    void*  buf = nullptr;
    size_t len = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &len);
    std::string r(static_cast<const char*>(buf), len);
    mz_zip_writer_end(&z);
    return r;
}

std::string sha(const std::string& s) { return bs::util::sha256Hex(s.data(), s.size()); }
std::string le(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// as quatro pecas falsas (mesma ordem e estrutura das oficiais) e um servidor que as entrega
struct Servidor {
    std::map<std::string, std::string> conteudo;  // url -> bytes
    std::map<std::string, int>         pedidos;
    std::vector<std::string>           nomes, urls, hashes;  // os ponteiros das Peca apontam para estas strings
    std::vector<portatil::Peca>        pecas;
    Servidor() {
        const std::string zigZip = zip({{"zig-windows-x86_64-0.13.0/zig.exe", "zig falso"},
                                        {"zig-windows-x86_64-0.13.0/LICENSE", "MIT"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/mingw/COPYING", "ZPL"},
                                        {"zig-windows-x86_64-0.13.0/lib/libcxx/LICENSE.TXT", "Apache"},
                                        // a poda (podado()): os de outros sistemas saem, os do Windows e os comuns ficam
                                        {"zig-windows-x86_64-0.13.0/lib/libc/include/any-windows-any/windows.h", "fica"},
                                        {"zig-windows-x86_64-0.13.0/lib/include/stddef.h", "fica"},
                                        {"zig-windows-x86_64-0.13.0/lib/libunwind/src/u.c", "fica"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/include/any-linux-any/linux.h", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/include/generic-glibc/g.h", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/musl/m.c", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/wasi/w.c", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/glibc/g.c", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/libc/darwin/d.tbd", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/tsan/t.cpp", "sai"},
                                        {"zig-windows-x86_64-0.13.0/lib/docs/i.html", "sai"},
                                        {"outra-pasta/nao-entra.txt", "x"}});
        const std::string cmakeZip = zip({{"cmake-3.30.5-windows-x86_64/bin/cmake.exe", "cmake falso"},
                                          {"cmake-3.30.5-windows-x86_64/bin/cmake-gui.exe", "nao entra"},
                                          {"cmake-3.30.5-windows-x86_64/share/cmake-3.30/Modules/X.cmake", "modulo"},
                                          {"cmake-3.30.5-windows-x86_64/doc/cmake/Copyright.txt", "BSD"},
                                          {"cmake-3.30.5-windows-x86_64/doc/cmake/html/index.html", "nao entra"}});
        const std::string ninjaZip = zip({{"ninja.exe", "ninja falso"}});
        const std::string copying  = "Apache License";
        const std::pair<std::string, std::string> v[] = {{"zig.zip", zigZip}, {"cmake.zip", cmakeZip}, {"ninja.zip", ninjaZip},
                                                         {"COPYING", copying}};
        for (const auto& [n, c] : v) {
            nomes.push_back(n), urls.push_back("https://exemplo.invalid/" + n), hashes.push_back(sha(c));
            conteudo[urls.back()] = c;
        }
        for (size_t i = 0; i < 4; ++i)
            pecas.push_back({nomes[i].c_str(), nomes[i].c_str(), urls[i].c_str(), hashes[i].c_str(), conteudo[urls[i]].size(), "teste"});
    }
    widescreen::Fetch fetch() {
        return [this](const std::string& url, const fs::path& dst, const std::atomic<bool>* cancel,
                      const std::function<void(uint64_t, uint64_t)>& on, std::string* err) {
            ++pedidos[url];
            if (cancel && cancel->load()) return *err = "cancelado", false;
            const auto it = conteudo.find(url);
            if (it == conteudo.end()) return *err = "HTTP 404", false;
            std::ofstream(dst, std::ios::binary) << it->second;
            if (on) on(it->second.size(), it->second.size());
            return true;
        };
    }
};

portatil::Pedido pedido(const fs::path& pasta, Servidor& s, const std::string& arvore) {
    portatil::Pedido p;
    p.pasta  = pasta;
    p.fetch  = s.fetch();
    p.pecas  = s.pecas;
    p.arvore = arvore;
    return p;
}

// o hash da arvore montada com as pecas falsas: uma montagem com um esperado errado e recusada (nada no lugar) e a mensagem
// traz o hash real
std::string aprender(const fs::path& raiz, Servidor& s) {
    auto        p = pedido(raiz / "aprender", s, std::string(64, '0'));
    std::string m;
    try {
        portatil::preparar(p);
    } catch (const std::runtime_error& e) {
        m = e.what();
    }
    const size_t i = m.find("hash da pasta ");
    BS_CHECK(i != std::string::npos && !fs::exists(p.pasta) && !fs::exists(fs::path(p.pasta).concat(".montando")));
    return i == std::string::npos ? std::string() : m.substr(i + 14, 64);
}

} // namespace

BS_TEST(compilador_portatil_monta_e_confere) {
    Pasta             d("monta");
    Servidor          s;
    const fs::path    pasta = d.p / "compilador" / portatil::kVersao;
    const std::string arv   = aprender(d.p, s);
    BS_CHECK(arv.size() == 64);
    std::vector<widescreen::Andamento> an;
    auto p      = pedido(pasta, s, arv);
    p.andamento = [&](const widescreen::Andamento& a) { an.push_back(a); };
    portatil::preparar(p);
    BS_CHECK(portatil::pronta(pasta, nullptr, arv) && portatil::hash_arvore(pasta) == arv);
    BS_CHECK(!portatil::pronta(pasta));  // o hash oficial e outro: pecas falsas nunca passam pelas oficiais
    BS_CHECK(le(pasta / "zig" / "zig.exe") == "zig falso" && le(pasta / "cmake" / "bin" / "cmake.exe") == "cmake falso");
    BS_CHECK(le(pasta / "bin" / "ninja.exe") == "ninja falso");
    BS_CHECK(fs::exists(pasta / "cmake" / "share" / "cmake-3.30" / "Modules" / "X.cmake"));
    // so o que o filtro aceita: nem a interface grafica e a documentacao do CMake, nem outra pasta do zip do Zig
    BS_CHECK(!fs::exists(pasta / "cmake" / "bin" / "cmake-gui.exe") && !fs::exists(pasta / "cmake" / "doc" / "cmake" / "html"));
    BS_CHECK(!fs::exists(pasta / "outra-pasta") && !fs::exists(pasta / "zig" / "outra-pasta"));
    // a poda: so o que serve ao x86_64-windows-gnu
    for (const char* fica : {"lib/libc/include/any-windows-any/windows.h", "lib/include/stddef.h", "lib/libunwind/src/u.c",
                             "lib/libc/mingw/COPYING", "lib/libcxx/LICENSE.TXT"})
        BS_CHECK(fs::exists(pasta / "zig" / fs::path(fica)));
    for (const char* sai : {"lib/libc/include/any-linux-any", "lib/libc/include/generic-glibc", "lib/libc/musl", "lib/libc/wasi",
                            "lib/libc/glibc", "lib/libc/darwin", "lib/tsan", "lib/docs"})
        BS_CHECK(!fs::exists(pasta / "zig" / fs::path(sai)));
    BS_CHECK(le(pasta / "licencas" / "mingw-w64-COPYING.txt") == "ZPL" && le(pasta / "licencas" / "Ninja-COPYING.txt") == "Apache License");
    const std::string tc = le(pasta / "bs-toolchain.cmake");
    BS_CHECK(tc.find("x86_64-windows-gnu") != std::string::npos && tc.find("bs-regras.cmake") != std::string::npos);
    BS_CHECK(!fs::exists(fs::path(pasta).concat(".montando")) && !fs::exists(fs::path(pasta).concat(".antiga")));
    // andamento: as 4 pecas e o fim
    BS_CHECK(!an.empty() && an.back().fim && an.back().quantos == 4);
    // de novo: pronta, nada baixado (o CMake foi pedido no aprender e na primeira montagem, nao uma terceira vez)
    portatil::preparar(pedido(pasta, s, arv));
    BS_CHECK(s.pedidos[s.urls[1]] == 2);
}

BS_TEST(compilador_portatil_qualquer_adulteracao_monta_de_novo) {
    Pasta             d("adulterado");
    Servidor          s;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p, s);
    portatil::preparar(pedido(pasta, s, arv));
    // cada adulteracao derruba a conferencia (revisao Codex 20261120-194725): o executavel, uma biblioteca do Zig, um modulo
    // do CMake, uma licenca, um arquivo a mais e um a menos
    const std::vector<std::function<void()>> adulteracoes = {
        [&] { std::ofstream(pasta / "zig" / "zig.exe", std::ios::binary | std::ios::trunc) << "trocado"; },
        [&] { std::ofstream(pasta / "zig" / "lib" / "libc" / "mingw" / "COPYING", std::ios::binary | std::ios::app) << "x"; },
        [&] { std::ofstream(pasta / "cmake" / "share" / "cmake-3.30" / "Modules" / "X.cmake", std::ios::binary | std::ios::app) << "x"; },
        [&] { std::ofstream(pasta / "licencas" / "libcxx-LICENSE.txt", std::ios::binary | std::ios::trunc) << "x"; },
        [&] { std::ofstream(pasta / "zig" / "lib" / "a-mais.h", std::ios::binary) << "x"; },
        [&] { fs::remove(pasta / "bs-regras.cmake"); },
    };
    for (const auto& adulterar : adulteracoes) {
        adulterar();
        std::string motivo;
        BS_CHECK(!portatil::pronta(pasta, &motivo, arv) && motivo.find("diferente") != std::string::npos);
        portatil::preparar(pedido(pasta, s, arv));  // monta de novo dos zips guardados (conferidos), sem baixar
        BS_CHECK(portatil::pronta(pasta, nullptr, arv) && le(pasta / "zig" / "zig.exe") == "zig falso");
    }
    BS_CHECK(s.pedidos[s.urls[0]] == 2);  // o zip do Zig: no aprender e na primeira montagem; as remontagens nao baixam
}

// a montagem nova que nao confere nunca troca a anterior: a boa fica no lugar
BS_TEST(compilador_portatil_montagem_ruim_mantem_a_anterior) {
    Pasta             d("mantem");
    Servidor          s;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p, s);
    portatil::preparar(pedido(pasta, s, arv));
    BS_CHECK(portatil::pronta(pasta, nullptr, arv));
    bool lancou = false;
    try {
        portatil::preparar(pedido(pasta, s, std::string(64, '1')));  // esperado outro: monta de novo e nao confere
    } catch (const std::runtime_error& e) {
        lancou = std::string(e.what()).find("nao confere") != std::string::npos;
    }
    BS_CHECK(lancou && portatil::pronta(pasta, nullptr, arv));
    BS_CHECK(!fs::exists(fs::path(pasta).concat(".antiga")));
}

// revisao Codex 20261120-214554: o processo morto entre os dois renames da troca deixa a boa em <pasta>.antiga; a proxima
// instalacao a devolve ao lugar, sem baixar nem montar
BS_TEST(compilador_portatil_recupera_troca_interrompida) {
    Pasta             d("recupera");
    Servidor          s;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p, s);
    portatil::preparar(pedido(pasta, s, arv));
    fs::rename(pasta, fs::path(pasta).concat(".antiga"));
    std::vector<std::string> log;
    auto                     p = pedido(pasta, s, arv);
    p.log                      = [&](const std::string& l) { log.push_back(l); };
    portatil::preparar(p);
    BS_CHECK(portatil::pronta(pasta, nullptr, arv) && !fs::exists(fs::path(pasta).concat(".antiga")));
    BS_CHECK(s.pedidos[s.urls[0]] == 2 && !log.empty() && log[0].find("recuperado") != std::string::npos);
}

// o cenario da revisao Codex 20261120-220357: a pasta presente mas ruim, a .antiga boa e uma .quarentena velha no caminho:
// a boa volta ao lugar, sem baixar nem montar, e nada se perde
BS_TEST(compilador_portatil_recupera_com_pasta_ruim_e_quarentena) {
    Pasta             d("recupera2");
    Servidor          s;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p, s);
    portatil::preparar(pedido(pasta, s, arv));
    fs::copy(pasta, fs::path(pasta).concat(".antiga"), fs::copy_options::recursive);
    fs::remove(pasta / "bs-regras.cmake");
    fs::create_directories(fs::path(pasta).concat(".quarentena") / "velha");
    const int antes = s.pedidos[s.urls[1]];
    portatil::preparar(pedido(pasta, s, arv));
    BS_CHECK(portatil::pronta(pasta, nullptr, arv) && !fs::exists(fs::path(pasta).concat(".antiga")));
    BS_CHECK(s.pedidos[s.urls[1]] == antes);  // nada baixado
}

// duas instalacoes ao mesmo tempo: uma monta, a outra espera a trava e so confere (o Zig baixado uma vez so)
BS_TEST(compilador_portatil_duas_ao_mesmo_tempo) {
    Pasta             d("duas");
    Servidor          s1, s2;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p / "a", s1);
    const int         antes = s1.pedidos[s1.urls[0]];
    bool              ok1 = false, ok2 = false;
    std::thread       t1([&] {
        try {
            portatil::preparar(pedido(pasta, s1, arv));
            ok1 = true;
        } catch (const std::exception&) {
        }
    });
    std::thread t2([&] {
        try {
            portatil::preparar(pedido(pasta, s2, arv));
            ok2 = true;
        } catch (const std::exception&) {
        }
    });
    t1.join(), t2.join();
    BS_CHECK(ok1 && ok2 && portatil::pronta(pasta, nullptr, arv));
    BS_CHECK((s1.pedidos[s1.urls[0]] - antes) + s2.pedidos[s2.urls[0]] == 1);
}

// a troca que nao consegue tirar a antiga do lugar (um arquivo aberto dentro dela) falha com a antiga no lugar e sem a
// pasta temporaria
// a trava vale para o uso inteiro (revisao Codex 20261120-220357): com o primeiro token vivo, uma segunda chamada (mesmo com
// a pasta pronta) fica esperando; solto o token, ela segue
BS_TEST(compilador_portatil_trava_cobre_o_uso) {
    Pasta             d("uso");
    Servidor          s1, s2;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p / "a", s1);
    std::shared_ptr<void> uso = portatil::preparar(pedido(pasta, s1, arv));
    std::atomic<bool>     terminou{false};
    std::thread           t([&] {
        try {
            portatil::preparar(pedido(pasta, s2, arv));
        } catch (const std::exception&) {
        }
        terminou = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    BS_CHECK(!terminou.load());  // ainda esperando a trava
    uso.reset();
    t.join();
    BS_CHECK(terminou.load() && s2.pedidos[s2.urls[0]] == 0);  // seguiu sem montar nada
}

BS_TEST(compilador_portatil_troca_bloqueada_mantem_a_antiga) {
    Pasta             d("bloqueada");
    Servidor          s;
    const fs::path    pasta = d.p / "c";
    const std::string arv   = aprender(d.p, s);
    portatil::preparar(pedido(pasta, s, arv));
    fs::remove(pasta / "bs-regras.cmake");  // a pasta deixa de conferir: a proxima monta de novo e tenta trocar
    std::string msg;
    {
        std::ifstream aberto(pasta / "zig" / "zig.exe", std::ios::binary);  // segura a pasta antiga
        try {
            portatil::preparar(pedido(pasta, s, arv));
        } catch (const std::runtime_error& e) {
            msg = e.what();
        }
    }
    // o Windows recusa renomear uma pasta com um arquivo aberto dentro (conferido em 2026-11-20)
    BS_CHECK(msg.find("anterior continua no lugar") != std::string::npos);
    BS_CHECK(fs::exists(pasta / "zig" / "zig.exe") && !fs::exists(fs::path(pasta).concat(".montando")));
    portatil::preparar(pedido(pasta, s, arv));  // sem o arquivo aberto, a troca segue
    BS_CHECK(portatil::pronta(pasta, nullptr, arv) && !fs::exists(fs::path(pasta).concat(".antiga")));
}

BS_TEST(compilador_portatil_hash_errado_e_cancelamento) {
    Pasta          d("hash");
    Servidor       s;
    const fs::path pasta = d.p / "c";
    s.conteudo[s.urls[1]] = "cmake adulterado";  // o servidor entrega outra coisa no lugar do CMake
    bool lancou           = false;
    try {
        portatil::preparar(pedido(pasta, s, std::string(64, '0')));
    } catch (const std::runtime_error& e) {
        lancou = std::string(e.what()).find("nao confere") != std::string::npos;
    }
    BS_CHECK(lancou);
    BS_CHECK(!fs::exists(pasta) && !portatil::pronta(pasta));
    BS_CHECK(!fs::exists(fs::path(pasta).concat(".downloads") / "cmake.zip"));
    // cancelado antes de comecar: nada no lugar
    Servidor          s2;
    std::atomic<bool> cancelar{true};
    auto              p = pedido(d.p / "c2", s2, std::string(64, '0'));
    p.cancel            = &cancelar;
    lancou              = false;
    try {
        portatil::preparar(p);
    } catch (const std::runtime_error&) {
        lancou = true;
    }
    BS_CHECK(lancou && !fs::exists(d.p / "c2"));
}

BS_TEST(compilador_portatil_nome_inseguro_recusado) {
    Pasta             d("inseguro");
    Servidor          s;
    const std::string ruim = zip({{"zig-windows-x86_64-0.13.0/../../fora.exe", "x"}});
    s.conteudo[s.urls[0]]  = ruim;
    s.hashes[0]            = sha(ruim);
    s.pecas[0].sha256      = s.hashes[0].c_str();
    bool lancou            = false;
    try {
        portatil::preparar(pedido(d.p / "c", s, std::string(64, '0')));
    } catch (const std::runtime_error&) {
        lancou = true;
    }
    BS_CHECK(lancou && !fs::exists(d.p / "fora.exe") && !fs::exists(d.p / "c") && !fs::exists(d.p / "c.montando"));
}

BS_TEST(compilador_portatil_lista_oficial) {
    // as pecas oficiais: so HTTPS, hash de 64 hexadecimais, e o resumo cita cada uma; o hash da arvore oficial fixado
    for (const portatil::Peca& pc : portatil::kPecas) {
        BS_CHECK(std::string(pc.url).rfind("https://", 0) == 0);
        BS_CHECK(std::string(pc.sha256).size() == 64);
    }
    BS_CHECK(std::string(portatil::kArvoreSha256).size() == 64);
    BS_CHECK(portatil::resumo().size() == std::size(portatil::kPecas));
    BS_CHECK(portatil::bytes_do_download() > 120000000 && portatil::bytes_do_download() < 130000000);
}
