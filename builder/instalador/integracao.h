#pragma once
// N9.1: a integracao de uma instalacao com o sistema, por usuario e sem administrador (decisao do mantenedor, 2026-11-04).
//   Windows (integracao_win.cpp): atalhos no Menu Iniciar (Programas do usuario) e, se pedido, na Area de Trabalho; entrada em
//     "Aplicativos instalados" em HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\BlueStingerRecomp-<chave da pasta>,
//     que chama o BlueStinger-Desinstalar.exe da propria instalacao.
//   Linux (integracao_posix.cpp): bluestinger-<chave>.desktop em $XDG_DATA_HOME/applications e, se pedido, na Area de
//     Trabalho do usuario (XDG_DESKTOP_DIR).
// BS_INSTALADOR_TESTE_RAIZ=<pasta> desvia tudo para dentro dela (menu/, desktop/, usuario/ e, no Windows, a chave
// HKCU\Software\BlueStingerRecomp\Teste\Uninstall): os testes nunca tocam no menu, na area de trabalho nem no registro reais.
#include "platform/edition.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bs::builder::instalador {

struct Atalhos {
    bool menu = true;
    bool areaDeTrabalho = false;
    bool steam = false;  // v0.9.7-1: o jogo como atalho nao-Steam na Steam (steam.h); desmarcado por padrao
};

struct DadosIntegracao {
    std::filesystem::path    pasta;          // pasta de saida (a instalacao)
    std::filesystem::path    exe;            // o jogo
    std::filesystem::path    desinstalador;  // BlueStinger-Desinstalar na pasta do jogo (vazio: sem entrada de desinstalacao)
    std::filesystem::path    icone;          // Windows: .exe ou .ico; Linux: .png (vazio: o do exe / nenhum)
    native::edition::Edition edicao = native::edition::Edition::Original;
    std::string              versao;
    uint64_t                 tamanhoKB = 0;
};

// Pasta do instalador na pasta do usuario (%LOCALAPPDATA%\BlueStingerRecomp; $XDG_DATA_HOME/BlueStingerRecomp).
std::filesystem::path pasta_usuario();
std::filesystem::path lista_instalacoes();  // pasta_usuario()/instalacoes.toml

// Nome que o jogador ve ("Blue Stinger" ou "Blue Stinger Remastered").
std::wstring nome_exibicao(native::edition::Edition e);

// Cria (ou refaz) os atalhos pedidos e a entrada de desinstalacao. Os atalhos de antes desta pasta (`anteriores`) saem.
// Devolve os atalhos criados; o que falhar vira uma linha em *aviso (a instalacao ja esta pronta e continua valendo).
std::vector<std::filesystem::path> integrar(const DadosIntegracao& d, const Atalhos& a,
                                            const std::vector<std::filesystem::path>& anteriores, std::string* aviso);
// Remove os atalhos dados e a entrada de desinstalacao da pasta.
void desintegrar(const std::filesystem::path& pasta, const std::vector<std::filesystem::path>& atalhos);

// A entrada de desinstalacao da pasta existe? (testes; Linux: sempre falso)
bool entrada_desinstalacao(const std::filesystem::path& pasta, std::wstring* comando = nullptr);

// O arquivo esta aberto por outro processo de um jeito que impede apaga-lo (Windows: o jogo rodando segura o executavel;
// Linux: sempre falso, um executavel rodando pode ser apagado).
bool arquivo_em_uso(const std::filesystem::path& f);

// Chave estavel da pasta (8 hex do FNV-1a do caminho dobrado), para nomes de atalho e da entrada.
std::string chave_pasta(const std::filesystem::path& pasta);

} // namespace bs::builder::instalador
