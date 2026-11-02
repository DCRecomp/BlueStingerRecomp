#pragma once
// N9.0 (docs/N9-0-EDICOES.md): o perfil de edicao. Um projeto e um nucleo, com duas edicoes escolhidas pelo instalador e
// gravadas em instalacao.toml ([edicao] perfil). O perfil nao e preferencia: o jogo o le uma vez ao abrir, e nem o menu,
// nem um save, nem a linha de comando o trocam. As preferencias continuam sendo do jogador; o perfil so as restringe,
// sem reescrever os arquivos dele (o mesmo controles.toml serve as duas edicoes).
#include "platform/camera_settings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bs::native::edition {

enum class Edition : uint8_t { Original, Remastered };

const char* edition_name(Edition e);                         // "original", "remastered"
bool        parse_edition(const std::string& v, Edition* out);  // falso: valor desconhecido (*out nao muda)

struct EditionProfile {
    Edition edition              = Edition::Original;
    bool    free_camera_required = false;  // camera Livre e movimento relativo a ela, sem opcao de desligar
    bool    operator==(const EditionProfile&) const = default;
};

EditionProfile profile_for(Edition e);

// A configuracao efetiva: o que o perfil proibe volta ao que ele exige. As outras opcoes da camera ficam como estao.
// `notes` recebe uma linha para cada item ignorado.
settings::CameraSettings clamp_to(settings::CameraSettings s, const EditionProfile& p, std::vector<std::string>* notes = nullptr);

// O que vai para o arquivo depois de uma mudanca: os itens impostos pelo perfil ficam como o jogador os deixou no
// arquivo; so os itens que ele pode mudar acompanham a configuracao aplicada.
settings::CameraSettings for_file(const settings::CameraSettings& applied, const settings::CameraSettings& persisted,
                                  const EditionProfile& p);

} // namespace bs::native::edition
