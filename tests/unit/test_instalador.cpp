// N9.1 (docs/N9-1-INSTALADOR.md): o nucleo do instalador, sem janela e sem o CDI. Pacotes falsos montados pela mesma troca
// segura do Builder (install_swap), numa pasta temporaria; a integracao com o sistema desviada por BS_INSTALADOR_TESTE_RAIZ.
#include "test.h"

#include "builder_core.h"
#include "child_process.h"
#include "instalador/fases.h"
#include "instalador/instalacoes.h"
#include "instalador/integracao.h"
#include "instalador/steam.h"
#include "platform/install_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace bs::builder;
using namespace bs::builder::instalador;

namespace {

struct TempDir {
    fs::path p;
    TempDir() {
        std::random_device rd;
        p = fs::temp_directory_path() / ("bs-instalador-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

void put(const fs::path& f, const std::string& s) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary | std::ios::trunc) << s;
}
std::string get(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

const wchar_t* exeName() {
#ifdef _WIN32
    return L"BlueStinger.exe";
#else
    return L"BlueStinger";
#endif
}

// pacote falso: o executavel, dados, um texto e o instalacao.toml (da instalacao, fora da lista)
fs::path stage(const fs::path& base, const std::string& versao, bs::native::edition::Edition ed) {
    const fs::path s = base / ("stage-" + versao);
    put(s / exeName(), "exe " + versao);
    put(s / "data" / "disc" / "1ST_READ.BIN", "dados");
    put(s / "VERSAO.txt", "Blue Stinger " + versao + "\n");
    put(s / "licencas" / "LICENSE.txt", "MIT");
    bs::native::install::InstallConfig c;
    c.edition = ed;
    bs::native::install::write_config(s, c);
    return s;
}

void instalar(const fs::path& pasta, const std::string& versao,
              bs::native::edition::Edition ed = bs::native::edition::Edition::Original) {
    fs::create_directories(pasta);
    install_swap(stage(pasta, versao, ed), pasta / kGameDir, [](const fs::path&) { return false; });
}

struct TesteRaiz {
    TempDir t;
    TesteRaiz() { _putenv_s("BS_INSTALADOR_TESTE_RAIZ", path_text(t.p).c_str()); }
    ~TesteRaiz() { _putenv_s("BS_INSTALADOR_TESTE_RAIZ", ""); }
};

} // namespace

BS_TEST(instalador_fases_monotonicas) {
    // a sequencia real do build(): etapas na ordem, com as fracoes das faixas de rangeOf
    const std::pair<Step, double> seq[] = {{Step::Validate, 0.0},  {Step::Toolchain, 0.02}, {Step::Extract, 0.05},
                                           {Step::Extract, 0.15},  {Step::Recompile, 0.20}, {Step::Configure, 0.30},
                                           {Step::Compile, 0.34},  {Step::Compile, 0.60},   {Step::Compile, 0.88},
                                           {Step::Package, 0.88},  {Step::Verify, 0.90},    {Step::Install, 0.98}};
    Progresso p;
    double    ultimo = -1;
    int       fase   = 0;
    for (const auto& [s, f] : seq) {
        const auto e = p.avancar(s, f);
        BS_CHECK(e.total >= ultimo);
        BS_CHECK(int(e.fase) >= fase);
        BS_CHECK(e.daFase >= 0 && e.daFase <= 1);
        ultimo = e.total;
        fase   = int(e.fase);
    }
    BS_CHECK(p.recuos() == 0);
    BS_CHECK(p.estado().fase == Fase::Instalacao);
    // a soma das faixas com arredondamento (0.34 + 0.54 * 1 vs 0.88) nao e recuo
    p.avancar(Step::Install, 0.98 + 1e-12);
    p.avancar(Step::Install, 0.98);
    BS_CHECK(p.recuos() == 0);
    ultimo = p.estado().total;
    BS_CHECK(fase_de(Step::Toolchain) == Fase::Extracao && fase_de(Step::Compile) == Fase::Preparacao &&
             fase_de(Step::Verify) == Fase::Instalacao);
    // um recuo do Builder nao aparece na tela e fica contado
    const auto e = p.avancar(Step::Compile, 0.5);
    BS_CHECK(e.total == ultimo && e.fase == Fase::Instalacao && p.recuos() == 1);
    BS_CHECK(p.concluir().total == 1.0);
    BS_CHECK(std::string(fase_chave(Fase::Preparacao)) == "fase.preparacao");
}

BS_TEST(instalador_lista_registra_e_esquece) {
    TempDir t;
    const fs::path lista = t.p / "usuario" / kListFile;
    const fs::path a = t.p / "Jogo A", b = t.p / "Jogo B", sem = t.p / "Sem jogo";
    instalar(a, "v1");
    instalar(b, "v2", bs::native::edition::Edition::Remastered);
    BS_CHECK(registrar(lista, a, {}));
    BS_CHECK(registrar(lista, b, {t.p / "menu" / "x.lnk"}));
    BS_CHECK(registrar(lista, sem, {}));                 // sem jogo: fica na lista, mas nao aparece
    BS_CHECK(registrar(lista, a / ".." / "Jogo A", {}));  // a mesma pasta: uma entrada so
    const auto l = listar(lista);
    BS_CHECK(l.size() == 2);
    if (l.size() == 2) {
        BS_CHECK(mesma_pasta(l[0].pasta, b) || mesma_pasta(l[0].pasta, a));
        const Instalacao& ib = mesma_pasta(l[0].pasta, b) ? l[0] : l[1];
        BS_CHECK(ib.edicao == bs::native::edition::Edition::Remastered);
        BS_CHECK(ib.versao == "Blue Stinger v2");
        BS_CHECK(ib.atalhos.size() == 1);
        BS_CHECK(l[0].instante >= l[1].instante);  // a mais recente primeiro
    }
    BS_CHECK(atalhos_registrados(lista, b).size() == 1);
    BS_CHECK(esquecer(lista, b));
    BS_CHECK(listar(lista).size() == 1);
    // arquivo danificado: lista vazia com aviso (o instalador abre)
    put(lista, "[[instalacao]\npasta = ");
    std::string aviso;
    BS_CHECK(listar(lista, &aviso).empty() && !aviso.empty());
}

BS_TEST(instalador_verificacao_do_pacote) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    const fs::path jogo = pasta / kGameDir;
    BS_CHECK(fs::exists(jogo / kPackageHashes));
    // os dados ficam fora dos hashes (o jogo confere com o manifesto dele); o executavel esta neles
    const std::string h = get(jogo / kPackageHashes);
    BS_CHECK(h.find("data/") == std::string::npos);
    BS_CHECK(h.find(narrow(exeName())) != std::string::npos);
    BS_CHECK(get(jogo / kPackageManifest).find(".bs-package.sha256") != std::string::npos);
    auto v = verificar_pacote(jogo);
    BS_CHECK(v.ok && !v.semHashes && v.conferidos > 3);
    // arquivo do jogador nao conta
    put(jogo / "saves" / "BLUESTIN.000", "save");
    BS_CHECK(verificar_pacote(jogo).ok);
    // alterado e faltando
    put(jogo / exeName(), "adulterado");
    fs::remove(jogo / "licencas" / "LICENSE.txt");
    v = verificar_pacote(jogo);
    BS_CHECK(!v.ok && v.diferentes.size() == 1 && v.faltando.size() == 1);
    // instalacao de antes do N9.1 (sem os hashes): so a lista
    fs::remove(jogo / kPackageHashes);
    put(jogo / "licencas" / "LICENSE.txt", "MIT");
    v = verificar_pacote(jogo);
    BS_CHECK(v.semHashes && !v.ok && v.faltando.size() == 1);  // o proprio .bs-package.sha256 da lista
    // cancelamento
    std::atomic<bool> c{true};
    BS_CHECK(verificar_pacote(jogo, {}, &c).cancelado);
}

BS_TEST(instalador_atualizacao_preserva_dados_pessoais) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    const fs::path jogo = pasta / kGameDir;
    put(jogo / "saves" / "BLUESTIN.000", "save 1");
    put(jogo / "graficos.toml", "[graficos]\n");
    put(jogo / "mods" / "meu" / "mod.toml", "id = 'meu'");
    const auto antes = impressao_pessoal(jogo);
    BS_CHECK(antes.size() == 3);  // os tres (o instalacao.toml e da instalacao: fora)
    instalar(pasta, "v2");
    BS_CHECK(impressao_pessoal(jogo) == antes);
    BS_CHECK(get(jogo / "VERSAO.txt") == "Blue Stinger v2\n");
    BS_CHECK(verificar_pacote(jogo).ok);
}

BS_TEST(instalador_desinstala_so_o_pacote) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    instalar(pasta, "v2");  // a v1 vai para BlueStinger.anterior
    const fs::path jogo = pasta / kGameDir;
    BS_CHECK(fs::exists(pasta / kPreviousDir / kInstallMarker));
    put(jogo / "saves" / "BLUESTIN.000", "save");
    put(jogo / "controles.toml", "[camera]\n");
    put(jogo / "bluestinger.log", "log do jogo");  // da instalacao (ao lado do executavel): sai, nao e do jogador
    put(pasta / kWorkDir / "builder.log", "log");
    put(pasta / kWorkDir / "data" / "x", "dados preparados");
    const fs::path fora = t.p / "fora.txt";
    put(fora, "nao e do jogo");
    // lista adulterada: um caminho para fora da pasta nao e apagado
    {
        std::ofstream m(jogo / kPackageManifest, std::ios::binary | std::ios::app);
        m << "../../fora.txt\n";
    }
    const auto antes = impressao_pessoal(jogo);
    const auto r     = desinstalar(pasta, {});
    BS_CHECK(r.ok);
    BS_CHECK(r.removidos > 5);
    BS_CHECK(fs::exists(fora));
    BS_CHECK(!fs::exists(jogo / exeName()) && !fs::exists(jogo / "data") && !fs::exists(jogo / kInstallMarker));
    BS_CHECK(get(jogo / "saves" / "BLUESTIN.000") == "save" && get(jogo / "controles.toml") == "[camera]\n");
    BS_CHECK(!fs::exists(pasta / kWorkDir));  // trabalho do Builder: pedido por padrao
    BS_CHECK(!r.pastaRemovida && !r.restantes.empty());
    // o que ficou e exatamente o que era do jogador; o instalacao.toml (da instalacao) saiu
    auto depois = impressao_pessoal(jogo);
    BS_CHECK(depois == antes);
    BS_CHECK(!fs::exists(jogo / bs::native::install::kConfigFile) && !fs::exists(jogo / "bluestinger.log"));
    BS_CHECK(antes.count("bluestinger.log") == 0);
    BS_CHECK(r.pendentes.empty());
    // a anterior: o pacote saiu
    BS_CHECK(!fs::exists(pasta / kPreviousDir / exeName()));
    // de novo: nao ha mais jogo
    BS_CHECK(desinstalar(pasta, {}).kind == ErrorKind::MissingInput);
}

BS_TEST(instalador_desinstala_limpa_e_preserva_trabalho_se_pedido) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    put(pasta / kWorkDir / "builder.log", "log");
    OpcoesDesinstalar op;
    op.trabalho = false;
    auto r = desinstalar(pasta, op);
    BS_CHECK(r.ok && r.restantes.empty() && fs::exists(pasta / kWorkDir / "builder.log") && !fs::exists(pasta / kGameDir));
    // sem trabalho e sem nada do jogador: a pasta sai
    instalar(pasta, "v1");
    r = desinstalar(pasta, {});
    BS_CHECK(r.ok && r.pastaRemovida && !fs::exists(pasta));
}

#ifdef _WIN32
BS_TEST(instalador_desinstala_nada_com_o_jogo_aberto) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    const fs::path exe = pasta / kGameDir / exeName();
    // o jogo aberto: o Windows nao deixa renomear a pasta de um arquivo aberto sem FILE_SHARE_DELETE
    HANDLE h = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    BS_CHECK(h != INVALID_HANDLE_VALUE);
    const auto r = desinstalar(pasta, {});
    CloseHandle(h);
    BS_CHECK(!r.ok && r.kind != ErrorKind::None);
    BS_CHECK(verificar_pacote(pasta / kGameDir).ok);  // nada mudou
}
#endif

BS_TEST(instalador_integracao_desviada) {
    TesteRaiz raiz;
    TempDir   t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1", bs::native::edition::Edition::Remastered);
    BS_CHECK(pasta_usuario() == raiz.t.p / "usuario");
    BS_CHECK(lista_instalacoes() == raiz.t.p / "usuario" / kListFile);
    DadosIntegracao d;
    d.pasta = pasta;
    d.exe   = pasta / kGameDir / exeName();
    d.desinstalador = pasta / kGameDir / "BlueStinger-Desinstalar";
    d.edicao = bs::native::edition::Edition::Remastered;
    d.versao = "v1";
    std::string aviso;
    auto        at = integrar(d, Atalhos{true, true}, {}, &aviso);
    BS_CHECK(aviso.empty());
    BS_CHECK(at.size() == 2);
    for (const auto& a : at) BS_CHECK(fs::exists(a) && a.native().find(raiz.t.p.native()) == 0);
#ifdef _WIN32
    std::wstring cmd;
    BS_CHECK(entrada_desinstalacao(pasta, &cmd) && cmd.find(L"--pasta") != std::wstring::npos);
    BS_CHECK(at[0].filename() == L"Blue Stinger Remastered.lnk");
#else
    const std::string desk = get(at[0]);
    BS_CHECK(desk.find("Name=Blue Stinger Remastered") != std::string::npos);
    BS_CHECK(desk.find("Exec=\"") != std::string::npos && desk.find("Categories=Game;") != std::string::npos);
#endif
    // refazer sem a area de trabalho: os anteriores saem
    auto at2 = integrar(d, Atalhos{true, false}, at, &aviso);
    BS_CHECK(at2.size() == 1);
    BS_CHECK(!fs::exists(at[1]) || at[1] == at2[0]);
    // outra instalacao com o mesmo nome no Windows: nao sobrescreve o atalho desta
    desintegrar(pasta, at2);
    for (const auto& a : at2) BS_CHECK(!fs::exists(a));
    BS_CHECK(!entrada_desinstalacao(pasta));
    BS_CHECK(chave_pasta(pasta) == chave_pasta(pasta / "." ) && chave_pasta(pasta).size() == 8);
}

// Revisao Codex 20261104-195545: o que uma lista adulterada (do pacote ou das instalacoes) nao pode fazer.
namespace {
bool cria_elo(const fs::path& alvo, const fs::path& elo) {
    std::error_code ec;
    fs::create_directory_symlink(alvo, elo, ec);
    if (!ec) return true;
#ifdef _WIN32
    // sem o privilegio de link simbolico: uma junction (o mesmo efeito para o desinstalador)
    const std::wstring c = L"cmd /d /c mklink /J \"" + elo.wstring() + L"\" \"" + alvo.wstring() + L"\" >nul";
    return _wsystem(c.c_str()) == 0 && fs::exists(elo);
#else
    return false;
#endif
}
} // namespace

BS_TEST(instalador_desinstala_nao_segue_elos) {
    TempDir t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    const fs::path jogo = pasta / kGameDir;
    const fs::path fora = t.p / "fora";
    put(fora / "vitima.txt", "de outro programa");
    // um componente intermediario da lista e um link (ou junction) para fora: o arquivo de la fica
    BS_CHECK(cria_elo(fora, jogo / "elo"));
    {
        std::ofstream m(jogo / kPackageManifest, std::ios::binary | std::ios::app);
        m << "elo/vitima.txt\n";
    }
    auto r = desinstalar(pasta, {});
    BS_CHECK(r.ok);
    BS_CHECK(get(fora / "vitima.txt") == "de outro programa");
    BS_CHECK(!fs::exists(jogo / exeName()));
    // a pasta do jogo inteira e um link para outra instalacao: recusado, nada muda
    const fs::path real = t.p / "Real";
    instalar(real, "v1");
    const fs::path pasta2 = t.p / "Jogo2";
    fs::create_directories(pasta2);
    BS_CHECK(cria_elo(real / kGameDir, pasta2 / kGameDir));
    r = desinstalar(pasta2, {});
    BS_CHECK(!r.ok);
    BS_CHECK(fs::exists(real / kGameDir / exeName()) && fs::exists(real / kGameDir / kPackageManifest));
    std::error_code ec;
    fs::remove(pasta2 / kGameDir, ec);  // so o link (antes do TempDir apagar a arvore)
    fs::remove(jogo / "elo", ec);
}

BS_TEST(instalador_atalhos_so_os_nossos) {
    TesteRaiz raiz;
    TempDir   t;
    const fs::path pasta = t.p / "Jogo";
    instalar(pasta, "v1");
    DadosIntegracao d;
    d.pasta  = pasta;
    d.exe    = pasta / kGameDir / exeName();
    d.edicao = bs::native::edition::Edition::Original;
    d.versao = "v1";
    // "atalhos" adulterados na lista: um arquivo qualquer, um atalho alheio no menu, um nome nosso fora do menu
    const fs::path vitima = t.p / "vitima.txt";
#ifdef _WIN32
    const fs::path alheio = raiz.t.p / "menu" / "Outro Jogo.lnk";
    const fs::path fora   = t.p / "Blue Stinger.lnk";
#else
    const fs::path alheio = raiz.t.p / "menu" / "outro-jogo.desktop";
    const fs::path fora   = t.p / "bluestinger-00000000.desktop";
#endif
    put(vitima, "v");
    put(alheio, "a");
    put(fora, "f");
    std::string aviso;
    const auto  at = integrar(d, Atalhos{true, false}, {vitima, alheio, fora}, &aviso);
    BS_CHECK(at.size() == 1);
    desintegrar(pasta, {vitima, alheio, fora});
    BS_CHECK(get(vitima) == "v" && get(alheio) == "a" && get(fora) == "f");
    desintegrar(pasta, at);
    for (const auto& a : at) BS_CHECK(!fs::exists(a));
#ifdef _WIN32
    // os dois nomes ja ocupados por arquivos de outra instalacao: um terceiro, sem gravar por cima
    const fs::path n1 = raiz.t.p / "menu" / "Blue Stinger.lnk", n2 = raiz.t.p / "menu" / "Blue Stinger (Jogo).lnk";
    put(n1, "alheio 1");
    put(n2, "alheio 2");
    const auto at3 = integrar(d, Atalhos{true, false}, {}, &aviso);
    BS_CHECK(at3.size() == 1 && !at3.empty() && at3[0].filename() == L"Blue Stinger (Jogo 2).lnk");
    BS_CHECK(get(n1) == "alheio 1" && get(n2) == "alheio 2");
    desintegrar(pasta, at3);
#endif
}

BS_TEST(instalador_lista_concorrente) {
    TempDir t;
    const fs::path lista = t.p / "usuario" / kListFile;
    auto           reg = [&](int base) {
        for (int i = 0; i < 15; ++i)
            registrar(lista, t.p / ("J" + std::to_string(base + i)), {t.p / ("a" + std::to_string(base + i))});
    };
    std::thread a(reg, 0), b(reg, 100);
    a.join();
    b.join();
    int n = 0;
    for (int base : {0, 100})
        for (int i = 0; i < 15; ++i) n += int(atalhos_registrados(lista, t.p / ("J" + std::to_string(base + i))).size());
    BS_CHECK(n == 30);  // nenhuma alteracao perdida
}

BS_TEST(instalador_espaco) {
    TempDir t;
    BS_CHECK(espaco_livre(t.p / "nao" / "existe") > 0);
    BS_CHECK(espaco_necessario(true) > espaco_necessario(false));
}

// v0.9.7-1: a opcao "Adicionar a Steam" (steam.h): o formato binario, a entrada sem duplicar, o que ja estava no arquivo
// preservado byte a byte, a remocao pela pasta, o arquivo de formato desconhecido intocado e a Steam desviada nos testes.
BS_TEST(instalador_steam_formato_e_entradas) {
    // o CRC-32 de referencia ("123456789" -> CBF43926) com o bit alto: o appid de atalho que a Steam calcula
    BS_CHECK(appid_atalho("12345678", "9") == 0xCBF43926u);
    BS_CHECK(appid_atalho("\"C:\\a.exe\"", "X") >= 0x80000000u);
    // um arquivo com uma entrada de outro programa (com um inteiro de 64 bits e uma tabela de tags)
    NoVdf raiz, lista, outra, tags;
    lista.tipo = 0x00, lista.nome = "shortcuts";
    outra.tipo = 0x00, outra.nome = "0";
    NoVdf n;
    n.tipo = 0x01, n.nome = "AppName", n.texto = "Outro jogo";
    outra.filhos.push_back(n);
    n.nome = "Exe", n.texto = "\"D:\\Outro\\outro.exe\"";
    outra.filhos.push_back(n);
    n = {};
    n.tipo = 0x07, n.nome = "Grande", n.numero = 0x0123456789ABCDEFull;
    outra.filhos.push_back(n);
    tags.tipo = 0x00, tags.nome = "tags";
    n = {};
    n.tipo = 0x01, n.nome = "0", n.texto = "favorito";
    tags.filhos.push_back(n);
    outra.filhos.push_back(tags);
    lista.filhos.push_back(outra);
    raiz.filhos.push_back(lista);
    const std::string original = gravar_vdf(raiz);
    NoVdf lido;
    BS_CHECK(ler_vdf(original, lido) && gravar_vdf(lido) == original);  // ida e volta byte a byte

    TempDir t;
    EntradaSteam e;
    e.nome = "Blue Stinger Remastered", e.pasta = t.p / "Jogo", e.exe = t.p / "Jogo" / exeName(), e.icone = e.exe;
    std::string um, dois, tres;
    BS_CHECK(incluir_entrada(original, e, um));
    BS_CHECK(ler_vdf(um, lido) && lido.filhos.size() == 1 && lido.filhos[0].filhos.size() == 2);
    const NoVdf* nova = lido.filhos[0].filho("1");
    BS_CHECK(nova && nova->filho("AppName") && nova->filho("AppName")->texto == e.nome);
    BS_CHECK(nova && nova->filho("Exe") && nova->filho("Exe")->texto.front() == '"' && nova->filho("appid") &&
             nova->filho("appid")->numero == appid_atalho(nova->filho("Exe")->texto, e.nome));
    // a entrada de antes, inteira (o mesmo trecho de bytes no comeco do arquivo novo)
    BS_CHECK(gravar_vdf(lido).compare(0, original.size() - 2, original, 0, original.size() - 2) == 0);
    // de novo, o mesmo executavel: atualiza, nao duplica
    e.nome = "Blue Stinger Remastered 2";
    BS_CHECK(incluir_entrada(um, e, dois) && ler_vdf(dois, lido) && lido.filhos[0].filhos.size() == 2);
    BS_CHECK(lido.filhos[0].filho("1")->filho("AppName")->texto == "Blue Stinger Remastered 2");
    // num arquivo vazio (a conta sem atalhos ainda)
    BS_CHECK(incluir_entrada("", e, tres) && ler_vdf(tres, lido) && lido.filhos.size() == 1 && lido.filhos[0].filhos.size() == 1);
    // a remocao: so a entrada que aponta para a pasta; a outra fica e o arquivo volta ao original
    std::string sem;
    int         tiradas = 0;
    BS_CHECK(tirar_entradas(dois, e.pasta, sem, &tiradas) && tiradas == 1 && sem == original);
    BS_CHECK(tirar_entradas(original, e.pasta, sem, &tiradas) && tiradas == 0 && sem == original);
    // formato desconhecido: nada muda
    std::string lixo = original;
    lixo[0] = char(0x05);
    BS_CHECK(!ler_vdf(lixo, lido) && !incluir_entrada(lixo, e, um) && !tirar_entradas(lixo, e.pasta, sem, &tiradas));
    BS_CHECK(!ler_vdf(original.substr(0, original.size() - 3), lido));  // cortado
}

BS_TEST(instalador_steam_desviada) {
    TesteRaiz r;
    BS_CHECK(pasta_steam().empty());  // sem <raiz>/steam: "Steam nao encontrada"
    std::string aviso;
    EntradaSteam e;
    e.nome = "Blue Stinger", e.pasta = r.t.p / "Jogo", e.exe = r.t.p / "Jogo" / exeName();
    BS_CHECK(adicionar_steam(e, &aviso) == 0 && aviso.find("Steam nao encontrada") != std::string::npos);
    const fs::path steam = r.t.p / "steam";
    fs::create_directories(steam / "userdata" / "12345" / "config");
    fs::create_directories(steam / "userdata" / "678");      // conta sem config ainda
    fs::create_directories(steam / "userdata" / "0");        // a pasta anonima: fica de fora
    fs::create_directories(steam / "userdata" / "anonimo");  // nao e conta
    put(steam / "userdata" / "678" / "config" / "shortcuts.vdf", std::string("\x05nada", 5));  // formato desconhecido
    fs::remove(steam / "userdata" / "678" / "config" / "shortcuts.vdf");
    fs::remove(steam / "userdata" / "678" / "config");
    BS_CHECK(pasta_steam() == steam);
    BS_CHECK(arquivos_atalhos_steam(steam).size() == 2);
    aviso.clear();
    BS_CHECK(adicionar_steam(e, &aviso) == 2 && aviso.empty());
    BS_CHECK(adicionar_steam(e, &aviso) == 2);  // de novo: a mesma entrada
    NoVdf lido;
    for (const auto& f : arquivos_atalhos_steam(steam)) {
        BS_CHECK(ler_vdf(get(f), lido) && lido.filhos.size() == 1 && lido.filhos[0].filhos.size() == 1);
        BS_CHECK(fs::exists(fs::path(f).concat(".anterior-bluestinger")));  // a segunda gravacao guardou a primeira
    }
    // um arquivo de formato desconhecido numa conta: nao e tocado e o aviso diz qual
    const fs::path ruim = steam / "userdata" / "12345" / "config" / "shortcuts.vdf";
    put(ruim, std::string("\x05nada", 5));
    aviso.clear();
    BS_CHECK(adicionar_steam(e, &aviso) == 1 && aviso.find("formato desconhecido") != std::string::npos);
    BS_CHECK(get(ruim) == std::string("\x05nada", 5));
    // a desinstalacao tira a entrada da outra conta
    aviso.clear();
    BS_CHECK(remover_steam(e.pasta, &aviso) == 1);
    BS_CHECK(ler_vdf(get(steam / "userdata" / "678" / "config" / "shortcuts.vdf"), lido) && lido.filhos[0].filhos.empty());
    BS_CHECK(get(ruim) == std::string("\x05nada", 5));
}
