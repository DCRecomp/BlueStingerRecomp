#pragma once
// N9.5 fase 5C (decisao D5-3 do mantenedor, 2026-11-10): telemetria de desempenho, no produto, desligada por padrao.
//
// BS_TELEMETRIA=<arquivo.csv> liga; BS_TELEMETRIA_DE=<vblank> e BS_TELEMETRIA_ATE=<vblank> limitam o trecho medido (no
// ATE o resumo sai e a medicao para). Regras:
//   - so leitura: nada do jogo, do render ou das configuracoes muda; nenhuma sincronizacao CPU/GPU nova (o render do
//     Vulkan ja espera a cerca de cada quadro; os tempos de GPU sao lidos depois dela);
//   - nada vai para a rede e nenhum dado pessoal e gravado: o arquivo tem numeros, o nome da GPU e a configuracao da
//     imagem; nenhum caminho, nome de usuario ou dado do jogo;
//   - o destino e o arquivo pedido, criado de forma atomica so se nao existir (nunca sobrescreve), numa pasta que tem de
//     existir (nada e criado) e que, resolvida (juncoes, links e unidades mapeadas seguidos), nao pode estar num volume de rede
//     (UNC, unidade remota; no Linux NFS, SMB/CIFS, AFS, 9P, Ceph); recusado, a telemetria fica desligada com um aviso;
//   - desligada, cada ponto custa a leitura de um bool: nenhum relogio, nenhuma alocacao, nenhuma consulta na GPU;
//   - o fim: finish() depois de parar as threads que produzem; o atexit e so um recurso que nunca bloqueia.
//
// O arquivo (CSV, uma linha por segundo de relogio, "na" = indisponivel): as linhas "# " no inicio dizem o que e medido,
// estimado e indisponivel nesta execucao; no fim, "# resumo" com os percentis da execucao inteira.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace bs::native::telemetry {

bool enabled();  // BS_TELEMETRIA lido uma vez (a primeira chamada abre o arquivo ou recusa)
// As regras do destino (testaveis): "" = aceito; senao, o motivo da recusa.
std::string target_problem(const std::filesystem::path& p);

// Categorias da memoria do Vulkan (alocacoes do proprio jogo pelo tamanho que o driver pediu: medido; o que o driver usa
// por fora delas nao entra).
enum class Mem : uint8_t { Targets, Slots, Textures, FrameBuffers, Post, Present, Other, Count };
const char* mem_name(Mem m);
void        mem_add(Mem m, bool device_local, int64_t bytes);  // + na alocacao, - na liberacao

// Cria o arquivo so se ele nao existir (atomico: CREATE_NEW / O_EXCL) e confere o volume do arquivo ABERTO (revisao Codex
// 20261110-041638: a pasta pode mudar entre a consulta e a abertura); recusado, `why` diz o motivo e nada e gravado (no
// Windows o arquivo criado e apagado pelo proprio handle; no Linux fica vazio, sem apagar por caminho).
std::FILE* create_exclusive(const std::filesystem::path& p, std::string& why);

// O relogio dos quadros (testavel): o frametime e o intervalo entre duas apresentacoes VISTAS seguidas. Uma volta sem
// quadro visto (a janela minimizada, a apresentacao falhou, o comeco do trecho) quebra a sequencia: a proxima apresentacao
// so recomeca a base (revisao Codex 20261110-041638: o tempo minimizado nao vira um frametime).
struct FrameClock {
    bool   based   = false;
    double last_ms = 0;
    // um quadro visto em `now_ms`: o frametime em ms, ou < 0 quando ele so estabelece a base
    double present(double now_ms) {
        const double ft = based ? now_ms - last_ms : -1.0;
        based = true, last_ms = now_ms;
        return ft;
    }
    void gap() { based = false; }
};

// Thread da janela (apresentacao): um quadro apresentado e visto agora; frame_gap: esta volta nao mostrou um quadro (a
// janela minimizada ou a apresentacao falhou). Sem janela (--headless), nenhum dos dois.
void frame_presented();
void frame_gap();
// Thread do jogo: um VBlank (a CPU da thread e lida aqui).
void game_tick();
// Thread da janela: a CPU dela (chamado junto do frame_presented e do tick).
void window_tick();

// Um render do Vulkan, em ms de GPU por etapa (timestamps; < 0 = indisponivel): envios de textura, raster (passes A e B e
// os volumes), resolve dos translucidos, saida (copia para o slot, reducao e leitura de volta).
struct GpuFrame {
    double uploads = -1, raster = -1, resolve = -1, output = -1;
};
void gpu_frame(const GpuFrame& g);
// O orcamento do driver (VK_EXT_memory_budget) da memoria local: uso e orcamento em bytes (-1 = indisponivel).
void set_budget(int64_t usage, int64_t budget);
// O contexto da imagem: o dispositivo, a resolucao interna e a proporcao em uso (sem caminhos nem nomes de usuario).
void set_device(const std::string& name, bool timestamps);
void set_image(uint32_t internal_height, bool wide);

// Uma volta do laco principal (janela ou sem janela): grava a linha do segundo que passou.
void tick();
// O fim do processo: a ultima linha e o resumo; fecha o arquivo.
void finish();

} // namespace bs::native::telemetry
