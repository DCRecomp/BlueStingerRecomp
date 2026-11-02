// N9.0 (docs/N9-0-EDICOES.md): perfil de edicao. Vem do instalacao.toml; o menu, a restauracao de padroes e as preferencias
// antigas do jogador nao o desviam, e o arquivo do jogador nao muda por causa dele.
#include "test.h"

#ifdef _WIN32
#include "builder_core.h"  // o Builder do Linux entra na L5 do N9-L
#endif
#include "platform/edition.h"
#include "platform/install_config.h"
#include "settings_menu.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ed = bs::native::edition;
namespace ni = bs::native::install;
namespace st = bs::native::settings;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path p;
    explicit Dir(const wchar_t* name) : p(fs::temp_directory_path() / name) {
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
    void toml(const std::string& text) const { std::ofstream(p / ni::kConfigFile, std::ios::binary) << text; }
};

std::vector<std::string> labels(const bs::SettingsMenu& m) {
    std::vector<std::string> out;
    for (const auto& i : m.view().items) out.push_back(i.label);
    return out;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void go_to_camera(bs::SettingsMenu& m) {
    m.open();
    for (int i = 0; i < 3; ++i) m.input(bs::MenuAction::Down, 0);  // Controles
    m.input(bs::MenuAction::Confirm, 0);
    m.input(bs::MenuAction::Confirm, 0);  // Camera
}

} // namespace

BS_TEST(edition_names_and_profiles) {
    ed::Edition e = ed::Edition::Remastered;
    BS_CHECK(ed::parse_edition("original", &e) && e == ed::Edition::Original);
    BS_CHECK(ed::parse_edition("remastered", &e) && e == ed::Edition::Remastered);
    BS_CHECK(!ed::parse_edition("Remastered", &e) && !ed::parse_edition("", &e) && e == ed::Edition::Remastered);
    BS_CHECK(!ed::profile_for(ed::Edition::Original).free_camera_required);
    BS_CHECK(ed::profile_for(ed::Edition::Remastered).free_camera_required);
    BS_CHECK(ed::EditionProfile{} == ed::profile_for(ed::Edition::Original));
}

BS_TEST(edition_read_from_install_config) {
    Dir d(L"bs-edition-config");
    std::string w;
    BS_CHECK(ni::read_config(d.p, &w).edition == ed::Edition::Original && w.empty());  // instalacao de antes do N9
    d.toml("[saves]\nlocal = \"portatil\"\n");
    BS_CHECK(ni::read_config(d.p, &w).edition == ed::Edition::Original && w.empty());
    d.toml("[edicao]\nperfil = \"remastered\"\n");
    BS_CHECK(ni::read_config(d.p, &w).edition == ed::Edition::Remastered && w.empty());
    for (const char* bad : {"[edicao]\nperfil = \"livre\"\n", "[edicao]\nperfil = 1\n"}) {
        d.toml(bad);
        w.clear();
        BS_CHECK(ni::read_config(d.p, &w).edition == ed::Edition::Original && w.find("edicao.perfil") != std::string::npos);
    }
    ni::InstallConfig c;
    c.edition = ed::Edition::Remastered, c.saves = ni::SavesLocation::Portable, c.subtitles = "pt-BR";
    BS_CHECK(ni::write_config(d.p, c));
    const ni::InstallConfig r = ni::read_config(d.p, &w);
    BS_CHECK(r.edition == ed::Edition::Remastered && r.saves == ni::SavesLocation::Portable && r.subtitles == "pt-BR");
}

BS_TEST(edition_clamps_camera_and_keeps_the_rest) {
    st::CameraSettings s;
    s.sens_x = 200, s.invert_y = true, s.movement = st::MoveMode::Original;
    std::vector<std::string> notes;
    BS_CHECK(ed::clamp_to(s, ed::profile_for(ed::Edition::Original), &notes) == s && notes.empty());
    const st::CameraSettings r = ed::clamp_to(s, ed::profile_for(ed::Edition::Remastered), &notes);
    BS_CHECK(r.mode == st::CameraMode::Free && r.movement == st::MoveMode::Camera && notes.size() == 2);
    BS_CHECK(r.sens_x == 200 && r.invert_y);
    notes.clear();
    BS_CHECK(ed::clamp_to(r, ed::profile_for(ed::Edition::Remastered), &notes) == r && notes.empty());
}

BS_TEST(edition_keeps_the_player_file) {
    st::CameraSettings disk;  // Original, gravado pela outra edicao ou antes dela
    disk.movement = st::MoveMode::Original;
    const auto rem = ed::profile_for(ed::Edition::Remastered);
    st::CameraSettings on = ed::clamp_to(disk, rem);
    on.sens_y             = 150;
    const st::CameraSettings f = ed::for_file(on, disk, rem);
    BS_CHECK(f.mode == disk.mode && f.movement == disk.movement && f.sens_y == 150);
    st::CameraSettings free;
    free.mode = st::CameraMode::Free;
    BS_CHECK(ed::for_file(free, disk, ed::profile_for(ed::Edition::Original)) == free);
}

BS_TEST(edition_menu_has_no_way_out_of_free_camera) {
    bs::Preferences p;
    p.subtitles.language = "en-US";
    bs::SettingsMenu orig(p, {}, {"en-US"});
    go_to_camera(orig);
    BS_CHECK(orig.page() == bs::SettingsMenu::Page::Camera && has(labels(orig), "Mode") && has(labels(orig), "Movement"));

    const auto rem = ed::profile_for(ed::Edition::Remastered);
    p.camera       = ed::clamp_to(p.camera, rem);
    bs::SettingsMenu m(p, {}, {"en-US"}, rem);
    go_to_camera(m);
    BS_CHECK(m.page() == bs::SettingsMenu::Page::Camera && !has(labels(m), "Mode") && !has(labels(m), "Movement"));
    BS_CHECK(has(labels(m), "Horizontal sensitivity") && m.prefs().camera.mode == st::CameraMode::Free);
    m.input(bs::MenuAction::Right, 0);  // a primeira opcao que sobrou muda, e a camera continua livre
    bs::Preferences got;
    BS_CHECK(m.take_changes(got) && got.camera.sens_x != p.camera.sens_x && got.camera.mode == st::CameraMode::Free);

    bs::SettingsMenu r(p, {}, {"en-US"}, rem);  // "Restaurar padroes" tambem nao tira
    r.open();
    for (int i = 0; i < 4; ++i) r.input(bs::MenuAction::Down, 0);
    r.input(bs::MenuAction::Confirm, 0);
    r.input(bs::MenuAction::Confirm, 0);
    BS_CHECK(r.prefs().camera.mode == st::CameraMode::Free && r.prefs().camera.movement == st::MoveMode::Camera);
}

#ifdef _WIN32
BS_TEST(edition_install_request_preserves_what_it_does_not_choose) {
    Dir d(L"bs-edition-request");
    ni::InstallConfig old;
    old.edition = ed::Edition::Remastered, old.saves = ni::SavesLocation::Portable, old.subtitles = "pt-BR";
    BS_CHECK(ni::write_config(d.p, old));

    bs::builder::Request update;  // Recompiler sem a escolha de edicao: atualizacao nao troca a edicao
    update.portableSaves       = false;
    ni::InstallConfig c        = bs::builder::install_config_for(update, d.p);
    BS_CHECK(c.edition == ed::Edition::Remastered && c.saves == ni::SavesLocation::Documents && c.subtitles == "pt-BR");

    bs::builder::Request change;  // o instalador escolheu
    change.edition = ed::Edition::Original;
    c              = bs::builder::install_config_for(change, d.p);
    BS_CHECK(c.edition == ed::Edition::Original && c.saves == ni::SavesLocation::Portable && c.subtitles == "pt-BR");

    Dir fresh(L"bs-edition-fresh");
    BS_CHECK(bs::builder::install_config_for(update, fresh.p).edition == ed::Edition::Original);
}
#endif
