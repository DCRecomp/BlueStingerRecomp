# Builder: do CDI do usuario ao BlueStinger.exe nativo (extracao, analise, geracao,
# compilacao com o compilador do usuario, verificacao e instalacao). Nao contem nada do jogo.
# N5: leitura dos filmes do disco (demux do SFD e ADX) para a conversao em MP4
# N9-L (L5): o mesmo nucleo no Linux (bs-builder; as janelas sao so do Windows). O que muda por sistema fica nos
# arquivos <nome>_win.cpp e <nome>_posix.cpp (BS_PLAT).
add_library(bs_video_convert STATIC video/sfd_source.cpp video/movie_manifest_disc.cpp)
if(WIN32)
  target_sources(bs_video_convert PRIVATE ${CMAKE_SOURCE_DIR}/native/video/sfd_media.cpp ${CMAKE_SOURCE_DIR}/native/video/pl_mpeg_impl.c
                 video/sfd_convert.cpp video/movie_build.cpp)
else()
  # o demux, o ADX e o pl_mpeg ja estao no bs_native_video do Linux (o jogo toca os SFD)
  target_sources(bs_video_convert PRIVATE video/movie_build_posix.cpp)
endif()
target_include_directories(bs_video_convert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg)
target_link_libraries(bs_video_convert PUBLIC bs_native_video PRIVATE bs_disc tomlplusplus::tomlplusplus)
target_compile_features(bs_video_convert PUBLIC cxx_std_20)
if(WIN32)
  add_executable(bs-video-probe video/probe_main.cpp)  # desenvolvimento: confere os SFD do disco
  add_executable(bs-musica audio/musica_main.cpp)  # desenvolvimento (N6 fase F): toca uma musica do disco em WAV
  target_link_libraries(bs-musica PRIVATE bs_native_audio_core)
  target_link_libraries(bs-video-probe PRIVATE bs_video_convert bs_disc)
  target_include_directories(bs-video-probe PRIVATE ${CMAKE_SOURCE_DIR}/third_party/pl_mpeg)
endif()

add_library(bs_builder_core STATIC builder_core.cpp builder_platform_${BS_PLAT}.cpp toolchain_${BS_PLAT}.cpp
            child_process_${BS_PLAT}.cpp project_export.cpp resource_inspect.cpp)
target_include_directories(bs_builder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_builder_core PUBLIC bs_recomp_lib bs_runtime bs_runtime_cdi bs_native PRIVATE bs_video_convert bs_native_video tomlplusplus::tomlplusplus)

if(WIN32)
  add_executable(bs-builder main_cli.cpp builder.manifest)
else()
  add_executable(bs-builder main_cli.cpp)
endif()
target_link_libraries(bs-builder PRIVATE bs_builder_core)

# Textos da interface por chave (pt-BR, en-US; um arquivo por idioma). N9.1: portavel (o instalador do Linux usa o mesmo)
add_library(bs_builder_ui STATIC ui/i18n.cpp ui/lang_pt_br.cpp ui/lang_en_us.cpp)
target_include_directories(bs_builder_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_builder_ui PUBLIC cxx_std_20)

# N9.1 (docs/N9-1-INSTALADOR.md): instalacoes do jogador, verificacao, desinstalacao, fases do progresso e integracao com o
# sistema (atalhos, desinstalador), sem janela. O que toca o sistema: instalador/integracao_<BS_PLAT>.cpp
add_library(bs_instalador_core STATIC instalador/instalacoes.cpp instalador/fases.cpp instalador/integracao.cpp
            instalador/integracao_${BS_PLAT}.cpp)
target_include_directories(bs_instalador_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_instalador_core PUBLIC bs_builder_core bs_native PRIVATE bs_disc_data tomlplusplus::tomlplusplus)
if(WIN32)
  target_link_libraries(bs_instalador_core PRIVATE ole32 shell32 advapi32)
endif()

# N9.1: as telas do instalador e a identidade visual, sem o sistema (o hospedeiro de cada um desenha pelo gui/pintor.h)
add_library(bs_instalador_gui STATIC instalador/gui/ui.cpp instalador/gui/arte.cpp instalador/gui/telas.cpp
            instalador/gui/hospedeiro.cpp)
target_include_directories(bs_instalador_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_instalador_gui PUBLIC bs_instalador_core bs_builder_ui bs_builder_core bs_native_api PRIVATE tomlplusplus::tomlplusplus)

if(NOT WIN32)
  # N9.1, Linux (decisao do mantenedor, 2026-11-04): o instalador numa janela SDL3, com o pintor em software e o texto pelo
  # native/text (FreeType + Noto Sans). O SDL3 e o pre-compilado do kit (BS_SDL3_PREBUILT), estatico como no jogo. Sem ele
  # (compilacao so do bs-builder), as janelas ficam de fora.
  if(NOT TARGET SDL3::SDL3-static AND BS_SDL3_PREBUILT)
    find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3-static PATHS "${BS_SDL3_PREBUILT}" NO_DEFAULT_PATH)
  endif()
  if(TARGET SDL3::SDL3-static)
    foreach(_bs_inst BlueStinger-Recompiler BlueStinger-Desinstalar)
      add_executable(${_bs_inst} instalador/gui/janela_sdl.cpp instalador/gui/pintor_sw.cpp)
      target_link_libraries(${_bs_inst} PRIVATE bs_instalador_gui bs_native_text bs_disc_data SDL3::SDL3-static)
    endforeach()
    target_compile_definitions(BlueStinger-Desinstalar PRIVATE BS_INSTALADOR_DESINSTALAR)
    add_dependencies(BlueStinger-Recompiler BlueStinger-Desinstalar)
  endif()
endif()

if(WIN32)

# Janelas Win32 nativas (DPI por monitor), sobre o mesmo bs_builder_core:
# - BlueStinger-Recompiler.exe: N9.1, o instalador (Win32 + Direct2D, decisao 12.2): telas do bs_instalador_gui;
# - BlueStinger-Desinstalar.exe: N9.1, vai no pacote do jogo (Aplicativos instalados o chama);
# - BlueStinger-Developer.exe: projeto C++ editavel, recompilar do zero e log tecnico.
# icone: gerado do emblema vetorial (arte.cpp) por uma ferramenta compilada aqui mesmo (nada de imagem no repositorio)
add_executable(bs-instalador-icone instalador/gui/icone_win.cpp instalador/gui/arte.cpp instalador/gui/pintor_d2d.cpp)
target_include_directories(bs-instalador-icone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs-instalador-icone PRIVATE bs_native_api d2d1 dwrite windowscodecs ole32)
set(BS_INSTALADOR_ICO ${CMAKE_CURRENT_BINARY_DIR}/instalador.ico)
add_custom_command(OUTPUT ${BS_INSTALADOR_ICO} COMMAND bs-instalador-icone ${BS_INSTALADOR_ICO}
                   DEPENDS bs-instalador-icone instalador/gui/arte.cpp ${PROJECT_SOURCE_DIR}/native/ui/identidade.h COMMENT "icone do instalador (emblema vetorial)")
configure_file(instalador/gui/instalador.rc.in ${CMAKE_CURRENT_BINARY_DIR}/instalador.rc @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/instalador.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/instalador.ico)
foreach(_bs_inst BlueStinger-Recompiler BlueStinger-Desinstalar)
  add_executable(${_bs_inst} WIN32 instalador/gui/main_win.cpp instalador/gui/janela_win.cpp instalador/gui/pintor_d2d.cpp
                 ${CMAKE_CURRENT_BINARY_DIR}/instalador.rc ${CMAKE_CURRENT_BINARY_DIR}/instalador.ico builder.manifest)
  target_link_libraries(${_bs_inst} PRIVATE bs_instalador_gui d2d1 dwrite windowscodecs dwmapi ole32 shell32)
endforeach()
target_compile_definitions(BlueStinger-Desinstalar PRIVATE BS_INSTALADOR_DESINSTALAR)
# o instalador leva o desinstalador para o pacote do jogo: os dois saem juntos
add_dependencies(BlueStinger-Recompiler BlueStinger-Desinstalar)
add_executable(BlueStinger-Developer WIN32 main_developer.cpp builder.manifest)
target_link_libraries(BlueStinger-Developer PRIVATE bs_builder_core bs_builder_ui tomlplusplus::tomlplusplus comctl32)
endif()
