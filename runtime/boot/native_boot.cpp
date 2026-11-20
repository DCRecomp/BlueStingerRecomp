// Boot nativo (ver native_boot.h). O estado foi medido na entrada do jogo no oraculo
// (snapshot 2026-10-14, BIOS HLE reios do Flycast) e conferido regiao por regiao:
//  - 1ST_READ.BIN em 0x8C010000: identico ao arquivo do disco;
//  - IP.BIN em 0x8C008000: identico, exceto a pilha do proprio IP.BIN (0x8C00F3B0+);
//  - 0x8C000000-0x8C007FFF: 0xFF, com variaveis do sistema, vetores e stubs das syscalls
//    e uma tabela em 0x8C003174 (bios_entry_state.inc);
//  - 0x8CDE0000-0x8CFFFFFF: restos da rotina de boot do IP.BIN (tabela de permutacao e
//    pedacos do executavel), NAO reproduzidos: o boot nativo deixa zeros. A regressao do
//    trace (STATUS.md, boot nativo) mostra se o jogo depende deles.
//  - registradores do PVR/Holly deixados pelo BIOS: bios_entry_state.inc.
#include "boot/native_boot.h"

#include "system/sound_bus.h"
#include "cpu/irq_bridge.h"
#include "cpu/sh4_intc.h"
#include "cpu/sh4_mem.h"
#include "cpu/sh4_runtime.h"
#include "disc/ip_bin.h"
#include "disc/iso9660.h"
#include "disc/scramble.h"
#include "system/syscall_service.h"
#include "boot_identity.h"
#include "versao/enderecos.h"  // BS_VERSAO_JOGO
#include "memory/sh4_ccn.h"
#include "memory/sh4_dmac.h"
#include "memory/sh4_port.h"
#include "memory/sh4_sq.h"
#include "memory/sh4_tmu.h"
#include "memory/sys_logical.h"
#include "memory/tmu_logical.h"
#include "pvr/holly_intc.h"
#include "pvr/holly_sb.h"
#include "pvr/pvr_regs.h"
#include "pvr/ta.h"
#include "pvr/vram.h"
#include "runtime_data.h"
#include "util/hash.h"

#include <cstring>
#include <ctime>
#include <array>
#include <initializer_list>
#include <stdexcept>

namespace bs {
namespace {

#include "boot/bios_entry_state.inc"

constexpr uint32_t kEntry = 0xAC010000;

} // namespace

BootFiles boot_read_data(const std::filesystem::path& dir) {
    BootFiles         f;
    disc::RuntimeDisc data(dir);  // lanca RuntimeDataError com a mensagem para o usuario
    try {
        f.ipBin              = data.ipBin();
        const disc::IpBin ip = disc::parseIpBin(f.ipBin.data(), f.ipBin.size());
        f.product = ip.product, f.version = ip.version, f.title = ip.title;
        // a mesma identificacao da preparacao (boot_identity.h), com a faixa de boot registrada nos dados
        BootIdentity id = identify_boot(data.readFile(ip.boot_file), data.manifest().bootTrackLba);
        if (id.version) {
            f.versionId = id.version->id;
            f.firstRead = std::move(id.data);
        }
    } catch (const disc::DiscError& err) {
        throw disc::RuntimeDataError(std::string("dados do jogo: ") + err.what());
    }
    if (f.versionId.empty())
        throw disc::RuntimeDataError("os dados do jogo nao sao da versao recompilada (hash do executavel de boot "
                                     "diferente). Gere a instalacao de novo com o Builder.");
    // PAL P4: o executavel so roda os dados da versao para a qual foi compilado (o C++ gerado e os enderecos do codigo
    // nativo, bs::jogo, sao de uma versao so)
    if (const int v = game_version_of(f.versionId); v != BS_VERSAO_JOGO)
        throw disc::RuntimeDataError(std::string("os dados do jogo sao de outra versao (") + f.versionId +
                                     ") que a deste executavel (" + bs::kVersaoNome +
                                     "). Gere a instalacao de novo com o Builder.");
    return f;
}

void boot_prepare(Sh4Context& c, const BootFiles& files, uint32_t rtcSeconds) {
    // Todo o hardware modelado volta ao reset: uma segunda sessao no mesmo processo nao
    // herda VRAM, interrupcoes pendentes, tempo do SPG, Maple, GD-ROM etc. (revisao Codex
    // 2026-10-14). Os dados do jogo para as leituras de setor sao configurados por quem chama.
    vram_reset();
    pvr_regs_reset();
    ta_reset();
    holly_intc_reset(0);
    holly_sb_reset();  // inclui o Maple
    syscall_service_reset();  // N8.3: pedidos do drive e flash em branco (o modelo HLE e da validacao)
    snd_bus_reset();  // N8.6: area de som (o modelo da AICA e da validacao)
    sh4_intc_reset();
    irq_bridge_reset();
    sh4_port_reset();
    sh4_sq_reset();
    sh4_dmac_reset();
    sh4_ccn_reset();
    sh4_tmu_reset();
    tmu_logical_reset();
    sys_logical_reset();
    uint8_t* ram = c.ram;
    std::memset(ram, 0, 16u << 20);
    std::memset(ram, 0xFF, 0x8000);  // area do sistema
    for (const auto& w : kBiosSysWords) std::memcpy(ram + (w[0] & 0xFFFFFF), &w[1], 4);
    std::memcpy(ram + 0x8000, files.ipBin.data(), files.ipBin.size());
    // A pilha do IP.BIN (0x8C00F3B0-0x8C00FFFF) e usada e limpa pela rotina de boot dele antes do jogo; o jogo le ali
    // a informacao de boot do sistema (0x8C00FFF0/FFF4, f_8C159E8E: zero = primeira entrada). O IP.BIN do CDI do
    // projeto tem zeros nessa area; o do disco original (GDI/CUE) tem o fim do codigo de boot e o titulo, e o jogo
    // seguia um ponteiro feito de espacos (0x20202020) e pedia o menu do sistema. Zerar reproduz o estado da entrada
    // medido no oraculo (para o CDI, nada muda). docs/IMAGENS-DISCO-PLANO.md, I4.
    std::memset(ram + 0xF3B0, 0, 0x10000 - 0xF3B0);
    std::memcpy(ram + 0x10000, files.firstRead.data(), files.firstRead.size());
    std::memcpy(ram + 0x78, &rtcSeconds, 4);  // hora do sistema, como o BIOS a deixa
    // Regiao na area do sistema (P7, 2026-11-20, BIOS real no oraculo): 8C000070/74 sao o codigo de regiao e transmissao
    // (americano "0011"/'0', PAL "0021"/'1', japones "0001"/'0') e 8C000068/6C, valores da flash da regiao. O americano fica
    // com os de kBiosSysWords: 70/74 iguais aos do BIOS real; 68/6C os do BIOS simulado (o real da outros, da flash).
#if BS_VERSAO_PAL
    for (const auto& w : std::initializer_list<std::array<uint32_t, 2>>{
             {0x0068, 0x7D988778}, {0x006C, 0x175ABD44}, {0x0070, 0x31323030}, {0x0074, 0x00000031}})
        std::memcpy(ram + w[0], &w[1], 4);
#elif BS_VERSAO_JOGO >= 3
    for (const auto& w : std::initializer_list<std::array<uint32_t, 2>>{
             {0x0068, 0x6A3014EB}, {0x006C, 0x7FC35DDB}, {0x0070, 0x31303030}})
        std::memcpy(ram + w[0], &w[1], 4);
#endif

    sh4_reset(c, kEntry);
    // Registradores na entrada (medidos): pilha e VBR em 0x8C00F400, GBR na area do
    // sistema, PR de volta para o IP.BIN. Os demais valores sao restos do IP.BIN/BIOS,
    // reproduzidos para que o estado seja o mesmo do oraculo.
    c.r[15] = 0x8C00F400;
    c.vbr   = 0x8C00F400;
    c.gbr   = 0x8C000000;
    c.pr    = 0xAC00E0B2;
    c.macl  = 0x00007080;
    c.ssr   = 0x40000001;
    c.spc   = 0x8C000776;
    const uint32_t fr3 = 0x38D1B717;
    std::memcpy(&c.fr[3], &fr3, 4);
    const uint32_t bank[8] = {0x700000F0, 0xFFFFFF0F, 0xFF00001C, 0, 0xAC00FC00, 0, 0x400, 0x400};
    std::memcpy(c.rbank, bank, sizeof bank);
    c.ret_expect = c.pr;
    c.next_event = 0;
    c.next_poll  = ~0ull;  // N8.1: o primeiro atendimento decide (cadencia B no tempo logico)
    sh4_irq_bind(&c);      // N8.1: quem pede atendimento sem contexto (event_status_raise) escreve neste

    std::vector<uint32_t> pvr(2048, 0);
    for (const auto& r : kBiosPvrState) pvr[r[0] / 4] = r[1];
#if BS_VERSAO_PAL
    // PAL (docs/PAL-PLANO.md P7, 2026-11-20): o que muda na entrada do jogo entre o BIOS real PAL e o NTSC, medido no oraculo
    // com o dc_boot.bin real e a flash Eur-Pal (tools/dev/flycast_oracle.py entrada-bios; os tres PAL dao o mesmo; o
    // americano, em CDI e GD, e o japones dao o NTSC). So o SPG e a posicao da imagem: o resto do estado acima (o do BIOS
    // simulado, medido em 14/10) fica igual ao americano. SPG_VBLANK_INT fica o do NTSC (00150104), como no BIOS real.
    for (const auto& r : std::initializer_list<std::array<uint32_t, 2>>{
             {0x00C8, 0x034B0000}, {0x00D0, 0x00000190}, {0x00D4, 0x008D034B}, {0x00D8, 0x0270035F},
             {0x00DC, 0x002C026C}, {0x00E0, 0x07D6A53F}, {0x00EC, 0x000000AE}, {0x00F0, 0x002E002D}})
        pvr[r[0] / 4] = r[1];
#endif
    pvr_regs_load(pvr.data());
    snd_bus_rtc_set(rtcSeconds, 0);
    syscall_service_install(&c);  // N8.3: vetores das syscalls -> servico nativo
}

uint32_t boot_rtc_now() {
    // 1950-01-01 a 1970-01-01: 20 anos com 5 bissextos = 7305 dias
    constexpr int64_t kEpochDelta = 7305ll * 86400;
    std::time_t       t           = std::time(nullptr);
    std::tm           local{};
    // o RTC do Dreamcast guarda a hora local (configurada pelo usuario no BIOS)
#ifdef _WIN32
    localtime_s(&local, &t);
    const int64_t localSecs = int64_t(_mkgmtime(&local));
#else
    localtime_r(&t, &local);
    const int64_t localSecs = int64_t(timegm(&local));
#endif
    return uint32_t(localSecs + kEpochDelta);
}

} // namespace bs
