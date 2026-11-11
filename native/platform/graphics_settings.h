#pragma once
// N7 (docs/N7-PLANO.md secoes 15 a 17): modelo das configuracoes graficas, separado da interface (o menu interno
// do jogo, depois do backend Vulkan estabilizar) e do renderizador (que so recebe o resultado). Persistencia em
// graficos.toml na pasta do usuario (ao lado de controles.toml); o mesmo padrao de configuracao preparado para
// audio, controles e acessibilidade.
//
// Presets: Original Dreamcast (a configuracao de equivalencia: resolucao do jogo, 4:3, amostragem original, sem
// efeitos), Equilibrado, Alta Qualidade e Personalizado (qualquer combinacao que nao seja um preset). O modo de
// tela, o V-Sync e a proporcao sao do jogador e nao fazem parte dos presets.
// Capacidades: o backend informa o que suporta e o que JA esta implementado (Features); clamp_to() leva a configuracao
// para o suportado (com o motivo), e sem Vulkan tudo cai no Original (o renderizador em software so faz o original).
// O menu so oferece como funcionando o que Features diz (nada de opcao de enfeite).
// Aplicacao (apply_kind): filtragem e efeitos valem no quadro seguinte; resolucao, escala, MSAA, modo de tela,
// proporcao, tamanho da janela e V-Sync recriam alvos/swapchain entre quadros (sem tocar o estado do jogo); as que
// mudam o tamanho ou o modo de tela pedem confirmacao com reversao automatica (requires_confirmation).
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace bs::native::settings {

inline constexpr const wchar_t* kGraphicsFile   = L"graficos.toml";
inline constexpr int            kConfirmSeconds = 15;  // mudanca de resolucao/modo de tela sem confirmacao: volta
// o filtro CRT desenha as 480 linhas do jogo com o perfil do feixe: precisa de pelo menos ~2 linhas da imagem por linha
inline constexpr uint32_t       kCrtMinHeight   = 1080;

enum class Preset : uint8_t { Original, Balanced, High, Custom };
enum class Resolution : uint8_t { Original, P720, P1080, P1440, P2160 };  // altura interna 480/720/1080/1440/2160
enum class WindowMode : uint8_t { Windowed, Fullscreen, Borderless };
// Auto: removido temporariamente (N9.5 fase 5, D5-1); fica no enum para ler arquivos antigos, e clamp_to o leva a 4:3
enum class Aspect : uint8_t { Original4x3, Wide16x9, Auto };
enum class Filter : uint8_t { Original, Bilinear, Anisotropic };
enum class Shadows : uint8_t { Original, Enhanced };
enum class Water : uint8_t { Original, Enhanced, High };
// N9.6 fase 1: a qualidade do SSAO (baixa: meia resolucao, 4 direcoes x 3 passos; media: meia resolucao, 8 x 4; alta: o
// alvo inteiro, 8 x 4). O mesmo numero de native::render::AoQuality
enum class SsaoQuality : uint8_t { Low, Medium, High };

struct GraphicsSettings {
    // do preset
    Resolution resolution  = Resolution::Original;
    uint32_t   scale_pct   = 100;  // escala sobre a resolucao interna (50 a 200)
    Filter     filter      = Filter::Original;
    uint32_t   anisotropy  = 16;   // 2 a 16 (so com Filter::Anisotropic)
    uint32_t   msaa        = 1;    // 1, 2, 4, 8
    bool       hd_textures = false;  // substituicoes HD por conteudo, quando houver pacote (N9)
    bool       lighting    = false;  // iluminacao aprimorada (so com dados confiaveis)
    Shadows    shadows     = Shadows::Original;
    Water      water       = Water::Original;  // N9.7: a agua aprimorada so nas superficies do catalogo (kMaterialWater)
    std::string shader;              // shader de pos-processamento de mod (mods\shaders\<nome>); "" = nenhum
    std::map<std::string, float> shader_params;  // os parametros dele (os ausentes ficam com o padrao do manifesto)
    bool       bloom = false, ssao = false, tonemap = false, crt = false;
    SsaoQuality ssao_quality = SsaoQuality::Medium;  // N9.6 fase 1 (so vale com `ssao`, que fica desligado por padrao)
    float      sharpen = 0, gamma = 1.0f, saturation = 1.0f;
    // N7c (secao 21): correcao de cor (passe interno de pos-processamento); desligada = a imagem como o jogo a desenha
    bool       color = false;
    float      brightness = 0, contrast = 1.0f;
    // tonemapping (`tonemap`): exposicao em EV, compressao das altas luzes (0 a 1) e tom das sombras (-0,5 a 0,5)
    float      exposure = 0, highlights = 0.5f, tone_shadows = 0;
    // bloom (`bloom`): limiar de luminancia (0 a 1), raio (fracao da altura) e intensidade (0 a 2)
    float      bloom_threshold = 0.7f, bloom_radius = 0.02f, bloom_strength = 0.6f;
    // filtro CRT (`crt`): linhas de varredura, mascara de abertura e vinheta (0 a 1)
    float      crt_scanlines = 0.6f, crt_mask = 0.3f, crt_vignette = 0.3f;
    // do jogador (fora dos presets)
    WindowMode window = WindowMode::Windowed;
    uint32_t   window_w = 0, window_h = 0;  // tamanho da janela (0 = o padrao); tela cheia usa a do monitor
    Aspect     aspect = Aspect::Original4x3;
    bool       vsync  = true;
    bool       vulkan = true;  // renderizador: Vulkan (padrao desde o fechamento do N7) ou o rasterizador em software
    bool       operator==(const GraphicsSettings&) const = default;
};

// O que o renderizador ja implementa (N7c gradual): o resto fica indisponivel no menu e volta ao valor original.
struct Features {
    bool scaling = false, msaa = false, filtering = false, window_modes = false, vsync = false, aspect_wide = false;
    bool hd_textures = false, lighting = false, shadows = false, shaders = false, post = false;
    bool color = false;    // N7c: correcao de cor (pos-processamento)
    bool tonemap = false;  // N7c: tonemapping; `post` = os efeitos de imagem ainda sem recurso proprio
    bool sharpen = false;  // N7c: nitidez
    bool bloom = false;    // N7c: bloom
    bool crt = false;      // N7c: filtro CRT (so com resolucao interna >= kCrtMinHeight)
    bool water = false;    // N9.7: a agua aprimorada (Remastered com o Vulkan; a Original nunca)
    bool ssao = false;     // N9.6 fase 1: oclusao ambiente (Remastered com o Vulkan e a apresentacao direta)
    static Features all() {
        Features f{true, true, true, true, true, true, true, true, true, true, true};
        f.color = f.tonemap = f.sharpen = f.bloom = f.crt = f.water = f.ssao = true;
        return f;
    }
};

struct Capabilities {
    bool     vulkan         = false;
    Features features;
    uint32_t max_msaa       = 1;
    uint32_t max_anisotropy = 1;
    uint32_t max_height     = 480;  // maior altura interna que cabe na memoria/limites da GPU (alvo 4:3)
    uint32_t max_height_wide = 0;   // N9.5 fase 5B: a mesma para o quadro largo (0 = igual a max_height)
    bool     wide_hud       = false;  // correcao do HUD em 16:9 disponivel
    // N9.5 fase 1: o quadro largo (hor+) existe nesta edicao e neste renderizador (Remastered + Vulkan com apresentacao
    // direta). Libera Aspect::Wide16x9 no graficos.toml e, desde a fase 5, a opcao 4:3/16:9 no menu (aspect_choice);
    // Aspect::Auto vale 4:3 (D5-1).
    bool     wide_frame     = false;
};

GraphicsSettings preset(Preset p);                   // o preset (Custom = Original)
Preset           classify(const GraphicsSettings& s);  // qual preset a configuracao e (ou Custom)
uint32_t         internal_height(const GraphicsSettings& s);  // altura interna em pixels, com a escala

// O que o renderizador consegue: cada opcao sem suporte volta ao valor seguro, com o motivo em *notes.
GraphicsSettings clamp_to(const GraphicsSettings& s, const Capabilities& caps, std::vector<std::string>* notes = nullptr);

// Mudanca que pede confirmacao com reversao automatica (resolucao, escala, modo de tela, tamanho da janela). A proporcao
// nao pede (N9.5 fase 5, D5-2): ela nao muda o tamanho da janela e vale na hora.
bool requires_confirmation(const GraphicsSettings& from, const GraphicsSettings& to);

// N9.5 fase 5A (decisoes D5-1, D5-2 e D5-4 do mantenedor, 2026-11-10): a proporcao.
// - D5-1: o "automatico" saiu do menu; um graficos.toml antigo com "auto"/"automatico" vale 4:3 (aviso na leitura).
// - D5-2: a troca 4:3 <-> 16:9 vale na hora, sem a confirmacao de 15 s; o arquivo guarda a ultima proporcao aplicada de
//   fato (aspect_for_file), nunca uma escolha que nao chegou a valer.
// - D5-4: na edicao Original a proporcao e 4:3 fixa; o menu mostra "4:3 (original)" sem permitir mudanca, e o arquivo
//   (que a outra edicao pode ler) nao e reescrito por ela.
enum class AspectChoice : uint8_t {
    Fixed,        // a edicao nao tem o quadro largo (Original): 4:3 fixo
    Unavailable,  // a Remastered sem a apresentacao Vulkan direta (o SDL nao desenha o quadro largo): 4:3
    NeedsVulkan,  // a Remastered com o renderizador em software escolhido: 4:3 ate voltar ao Vulkan
    Free,         // 4:3 ou 16:9
};
// `edition_wide` = EditionProfile::wide_frame; `caps.wide_frame` = o quadro largo existe nesta execucao.
AspectChoice aspect_choice(const GraphicsSettings& s, const Capabilities& caps, bool edition_wide);
// A proporcao que vale na imagem: 16:9 so com a escolha Free e o 16:9 escolhido.
Aspect effective_aspect(const GraphicsSettings& s, const Capabilities& caps, bool edition_wide);
// A proporcao que vai para o graficos.toml: a do arquivo quando o jogador nao pode escolher (`choice` != Free: a Original,
// sem o quadro largo nesta execucao, com o software), quando ela veio da linha de comando (`--proporcao`, so nesta
// execucao) ou quando a escolha nao chegou a valer (`effective` diferente); senao, a escolhida (que e a aplicada).
Aspect aspect_for_file(Aspect selected, Aspect effective, Aspect on_file, AspectChoice choice, bool from_command_line);

// N9.5 fase 5B: o limite da resolucao interna acompanha a proporcao em uso. `gpu_max_4x3` = VulkanStats::max_internal_height
// (calculado para um alvo 4:3); o quadro largo tem 854/640 da largura, e o limite cai pelo fator da largura
// ((480/854) / (3/4)), o mais conservador entre a memoria e a dimensao maxima da imagem. `cap` = o maior degrau do menu.
uint32_t max_internal_height(Aspect effective, uint32_t gpu_max_4x3, uint32_t cap = 2160);
// A altura interna que vale com o limite `max_height`: a escolhida, reduzida pelos degraus do menu (a escala volta a 100 %,
// depois a resolucao desce) ate caber. A escolha do jogador nao muda: de volta ao 4:3, a altura escolhida volta.
uint32_t effective_internal_height(const GraphicsSettings& s, uint32_t max_height);
uint32_t effective_internal_height(uint32_t wanted, uint32_t max_height);  // uma altura pedida (a --resolucao-interna)

enum class Apply : uint8_t { None, Immediate, Recreate };  // nada mudou / no proximo quadro / recriar alvos e swapchain
Apply apply_kind(const GraphicsSettings& from, const GraphicsSettings& to);

std::string      to_toml(const GraphicsSettings& s);
GraphicsSettings from_toml(const std::string& text, std::string* warning = nullptr);  // invalido: padrao + aviso
GraphicsSettings read_graphics(const std::filesystem::path& dir, std::string* warning = nullptr);  // ausente: Original
bool write_graphics(const std::filesystem::path& dir, const GraphicsSettings& s, std::string* err = nullptr,
                    std::error_code* code = nullptr);

const char* preset_name(Preset p);  // "original", "equilibrado", "alta", "personalizado"

} // namespace bs::native::settings
