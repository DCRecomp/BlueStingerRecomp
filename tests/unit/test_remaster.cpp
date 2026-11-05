// N9.2 (docs/N9-2-INTERFACE.md): a interface nativa da Remastered sem janela nem jogo. O modelo (navegacao, confirmacoes,
// perfis, Continuar) e o desenho (todas as telas e as paginas das configuracoes, nos dois idiomas, de 800 x 600 a 3840 x 2160,
// sem texto cortado). BS_REMASTER_CAPTURAS=<pasta> grava as imagens (BMP) para conferencia.
#include "test.h"

#include "remaster/desenho.h"
#include "remaster/entrada_titulo.h"
#include "remaster/interface.h"
#include "settings_menu.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace rm = bs::remaster;
using bs::MenuAction;

namespace {

std::vector<rm::Perfil> dois_perfis() {
    return {{0, 1700000000, true}, {2, 1700005000, true}, {4, 1700009000, false}};  // o 4 e o mais novo, mas danificado
}
int achar(const rm::Vista& v, const std::string& r) {
    for (size_t i = 0; i < v.itens.size(); ++i)
        if (v.itens[i].rotulo == r) return int(i);
    return -1;
}
void ate(rm::Interface& ui, const std::string& r) {
    for (int i = 0; i < 20 && ui.vista().itens[size_t(ui.vista().selecionado)].rotulo != r; ++i) ui.input(MenuAction::Down);
}

void gravar_bmp(const std::filesystem::path& p, const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    std::ofstream f(p, std::ios::binary);
    const uint32_t tam = 54 + w * h * 4;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("BM", 2), u32(tam), u32(0), u32(54), u32(40), u32(w), u32(uint32_t(-int32_t(h))), u16(1), u16(32), u32(0), u32(w * h * 4),
        u32(2835), u32(2835), u32(0), u32(0);
    // a camada e pre-multiplicada sobre transparente: para ver, sobre um cinza medio (o "jogo")
    for (uint32_t v : px) {
        const uint32_t a = v >> 24, inv = 255 - a, fundo = 0x50;
        const uint8_t  b[4] = {uint8_t((v & 0xFF) + fundo * inv / 255), uint8_t(((v >> 8) & 0xFF) + fundo * inv / 255),
                              uint8_t(((v >> 16) & 0xFF) + fundo * inv / 255), 255};
        f.write(reinterpret_cast<const char*>(b), 4);
    }
}

} // namespace

BS_TEST(remaster_menu_inicial_itens_e_continuar) {
    rm::Interface ui;
    ui.abrir_inicial({});
    auto v = ui.vista();
    BS_CHECK(v.sobreTitulo && v.tela == rm::Tela::Inicial);
    for (const char* r : {"Novo jogo", "Continuar", "Carregar", "Configurações", "Extras", "Sair"}) BS_CHECK(achar(v, r) >= 0);
    // sem perfil: Continuar e Carregar desativados, a selecao comeca em Novo jogo e pula os desativados
    BS_CHECK(!v.itens[size_t(achar(v, "Continuar"))].ativo && !v.itens[size_t(achar(v, "Carregar"))].ativo);
    BS_CHECK(v.selecionado == 0);
    ui.input(MenuAction::Down);
    BS_CHECK(ui.vista().itens[size_t(ui.vista().selecionado)].rotulo == "Configurações");
    ui.input(MenuAction::Back);  // o menu inicial nao fecha
    BS_CHECK(ui.aberta() && !ui.tomar());
    // com perfis: Continuar selecionado e leva o valido mais recente (o 2; o 4 esta danificado)
    ui.abrir_inicial(dois_perfis());
    v = ui.vista();
    BS_CHECK(v.itens[size_t(v.selecionado)].rotulo == "Continuar" && v.itens[size_t(v.selecionado)].detalhe.find("Perfil 3") == 0);
    ui.input(MenuAction::Confirm);
    auto s = ui.tomar();
    BS_CHECK(s && s->pedido == rm::Pedido::Carregar && s->arquivo == 2);
    BS_CHECK(!ui.tomar());  // uma vez so
    ate(ui, "Novo jogo");
    ui.input(MenuAction::Confirm);
    s = ui.tomar();
    BS_CHECK(s && s->pedido == rm::Pedido::NovoJogo);
}

BS_TEST(remaster_perfis_sem_vmu_e_confirmacao_na_pausa) {
    rm::Interface ui;
    ui.abrir_inicial(dois_perfis());
    ate(ui, "Carregar");
    ui.input(MenuAction::Confirm);
    auto v = ui.vista();
    BS_CHECK(v.tela == rm::Tela::Perfis && v.itens.size() == 4);  // tres perfis e Voltar
    for (const auto& i : v.itens) BS_CHECK(i.rotulo.find("VMU") == std::string::npos && i.rotulo.find("Slot") == std::string::npos);
    BS_CHECK(!v.itens[2].ativo && v.itens[2].detalhe == "Arquivo danificado");
    ui.input(MenuAction::Down);  // Perfil 3
    ui.input(MenuAction::Confirm);
    auto s = ui.tomar();  // no menu inicial nao ha partida a perder: sem confirmacao
    BS_CHECK(s && s->pedido == rm::Pedido::Carregar && s->arquivo == 2);
    // Voltar das telas de dentro restaura a anterior e a selecao
    ui.abrir_inicial(dois_perfis());
    ate(ui, "Carregar");
    ui.input(MenuAction::Confirm);
    ui.input(MenuAction::Back);
    BS_CHECK(ui.tela() == rm::Tela::Inicial && ui.vista().itens[size_t(ui.vista().selecionado)].rotulo == "Carregar");
    // na pausa: carregar pede confirmacao (perde o progresso); Cancelar volta aos perfis
    ui.abrir_pausa(dois_perfis());
    v = ui.vista();
    BS_CHECK(!v.sobreTitulo && !v.itens[size_t(achar(v, "Salvar"))].ativo);  // Salvar: N9.3
    ate(ui, "Carregar");
    ui.input(MenuAction::Confirm);
    ui.input(MenuAction::Confirm);  // Perfil 1
    BS_CHECK(ui.tela() == rm::Tela::Confirmar && !ui.tomar());
    BS_CHECK(ui.vista().texto.find("progresso") != std::string::npos);
    ui.input(MenuAction::Down), ui.input(MenuAction::Confirm);  // Cancelar
    BS_CHECK(ui.tela() == rm::Tela::Perfis && !ui.tomar());
    ui.input(MenuAction::Confirm);  // Perfil 1 de novo, agora Sim
    ui.input(MenuAction::Confirm);
    s = ui.tomar();
    BS_CHECK(s && s->pedido == rm::Pedido::Carregar && s->arquivo == 0);
    // Menu principal e Sair tambem confirmam; Voltar na pausa retoma
    ui.abrir_pausa({});
    ate(ui, "Menu principal");
    ui.input(MenuAction::Confirm);
    BS_CHECK(ui.tela() == rm::Tela::Confirmar);
    ui.input(MenuAction::Confirm);
    s = ui.tomar();
    BS_CHECK(s && s->pedido == rm::Pedido::MenuPrincipal);
    ui.abrir_pausa({});
    ui.input(MenuAction::Back);
    s = ui.tomar();
    BS_CHECK(s && s->pedido == rm::Pedido::Retomar);
    // a espera ignora a entrada; o erro volta a origem
    ui.abrir_inicial({});
    ui.aguardar("Carregando");
    ui.input(MenuAction::Confirm);
    BS_CHECK(ui.tela() == rm::Tela::Aguarde && !ui.tomar());
    ui.erro("falhou");
    ui.input(MenuAction::Confirm);
    BS_CHECK(ui.tela() == rm::Tela::Inicial);
}

BS_TEST(remaster_ingles_e_acoes) {
    rm::Interface ui(true);
    ui.abrir_pausa(dois_perfis());
    const auto v = ui.vista();
    for (const char* r : {"Resume", "Save", "Load", "Settings", "Main menu", "Quit game"}) BS_CHECK(achar(v, r) >= 0);
    BS_CHECK(rm::Interface::data_texto(0, true) == "date unknown");
    rm::Acao a;
    BS_CHECK(rm::acao_de_nome("carregar", a) && a == rm::Acao::Carregar && !rm::acao_de_nome("x", a));
    BS_CHECK(std::string(rm::acao_nome(rm::Acao::NovoJogo)) == "novojogo");
}

BS_TEST(remaster_desenho_sem_cortes_em_resolucoes_e_idiomas) {
    rm::Desenho d;
    BS_CHECK(d.texto_ok());  // as fontes vem embutidas nos dois sistemas: sem elas e falha (revisao Codex 20261104-222855)
    if (!d.texto_ok()) return;
    const char* dir = std::getenv("BS_REMASTER_CAPTURAS");
    const std::pair<uint32_t, uint32_t> tamanhos[] = {{800, 600}, {1024, 768}, {1280, 720}, {1366, 768}, {1920, 1080},
                                                      {2560, 1080}, {2560, 1440}, {3840, 2160}};
    size_t imagens = 0, cortes = 0;
    for (bool en : {false, true}) {
        rm::Interface ui(en);
        ui.set_versao("v0.9.0-teste");
        // as telas da Remastered
        std::vector<std::pair<std::string, rm::Vista>> telas;
        ui.abrir_inicial(dois_perfis()), telas.push_back({"inicial", ui.vista()});
        ui.abrir_inicial({}), telas.push_back({"inicial-vazio", ui.vista()});
        ui.abrir_inicial(dois_perfis()), ate(ui, en ? "Load" : "Carregar"), ui.input(MenuAction::Confirm), telas.push_back({"perfis", ui.vista()});
        ui.abrir_inicial({}), ate(ui, "Extras"), ui.input(MenuAction::Confirm), telas.push_back({"extras", ui.vista()});
        ui.abrir_inicial({}), ui.aguardar(en ? "Loading profile 1..." : "Carregando o perfil 1..."), telas.push_back({"aguarde", ui.vista()});
        ui.abrir_inicial({}), ui.erro(en ? "Profile 1 could not be loaded: the file does not match this game." :
                                            "Não foi possível carregar o perfil 1: o arquivo não é deste jogo."),
            telas.push_back({"erro", ui.vista()});
        ui.abrir_pausa(dois_perfis()), telas.push_back({"pausa", ui.vista()});
        ui.abrir_pausa(dois_perfis()), ate(ui, en ? "Main menu" : "Menu principal"), ui.input(MenuAction::Confirm), telas.push_back({"confirmar", ui.vista()});
        // as paginas das configuracoes (o mesmo modelo do menu interno)
        bs::Preferences p;
        p.subtitles.language = en ? "en-US" : "pt-BR";
        bs::native::settings::Capabilities caps;
        caps.vulkan = true, caps.max_msaa = 8, caps.max_anisotropy = 16, caps.max_height = 2160;
        bs::SettingsMenu cfg(p, caps, {"en-US", "pt-BR"}, bs::native::edition::profile_for(bs::native::edition::Edition::Remastered));
        std::vector<std::pair<std::string, bs::MenuView>> paginas;
        cfg.open();
        const size_t principais = cfg.view().items.size();
        paginas.push_back({"config", cfg.view()});
        for (size_t k = 0; k + 1 < principais; ++k) {
            cfg.open();
            for (size_t j = 0; j < k; ++j) cfg.input(MenuAction::Down, 0);
            if (!cfg.view().items[k].submenu) continue;
            cfg.input(MenuAction::Confirm, 0);
            paginas.push_back({"config-" + std::to_string(k), cfg.view()});
        }
        for (const auto& [w, h] : tamanhos) {
            for (const auto& [nome, v] : telas) {
                const auto& px = d.camada(v, w, h, rm::area_4x3(w, h));
                for (const auto& c : d.cortes())
                    std::fprintf(stderr, "  corte %s %s %ux%u: \"%s\" precisa %.0f, cabe %.0f\n", en ? "en" : "pt", nome.c_str(), w, h,
                                 c.texto.c_str(), c.precisa, c.cabe);
                cortes += d.cortes().size(), ++imagens;
                if (dir) gravar_bmp(std::filesystem::path(dir) / (nome + (en ? "-en-" : "-pt-") + std::to_string(w) + "x" + std::to_string(h) + ".bmp"), px, w, h);
            }
            for (const auto& [nome, v] : paginas) {
                const auto& px = d.camada_config(v, w, h, w % 2 == 0, rm::area_4x3(w, h));
                for (const auto& c : d.cortes())
                    std::fprintf(stderr, "  corte %s %s %ux%u: \"%s\" precisa %.0f, cabe %.0f\n", en ? "en" : "pt", nome.c_str(), w, h,
                                 c.texto.c_str(), c.precisa, c.cabe);
                cortes += d.cortes().size(), ++imagens;
                if (dir) gravar_bmp(std::filesystem::path(dir) / (nome + (en ? "-en-" : "-pt-") + std::to_string(w) + "x" + std::to_string(h) + ".bmp"), px, w, h);
            }
        }
    }
    std::fprintf(stderr, "  %zu imagens, %zu cortes\n", imagens, cortes);
    BS_CHECK(imagens > 100 && cortes == 0);
}

BS_TEST(remaster_desenho_mouse) {
    rm::Desenho d;
    BS_CHECK(d.texto_ok());
    rm::Interface ui;
    ui.abrir_pausa({});
    d.camada(ui.vista(), 1280, 720, rm::area_4x3(1280, 720));
    // o centro do cartao cai numa linha; o canto, em nenhuma
    bool alguma = false;
    for (float y = 0; y < 720; y += 4)
        if (d.item_em(640, y)) alguma = true;
    BS_CHECK(alguma && !d.item_em(2, 2));
}
