// N9.1, Windows: as entradas dos dois executaveis do instalador (janela_win.cpp). BS_INSTALADOR_DESINSTALAR escolhe qual.
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace bs::builder::instalador::gui {
int principal_instalador(HINSTANCE inst, int show);
int principal_desinstalador(HINSTANCE inst, int show);
} // namespace bs::builder::instalador::gui

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
#ifdef BS_INSTALADOR_DESINSTALAR
    return bs::builder::instalador::gui::principal_desinstalador(inst, show);
#else
    return bs::builder::instalador::gui::principal_instalador(inst, show);
#endif
}
