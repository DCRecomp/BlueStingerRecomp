# Sistemas nativos definitivos (docs/PLANO-PORT-NATIVO.md). C++20 puro, sem Sh4Context,
# enderecos do guest ou registradores (conferido por n0_native_limpo).
add_library(bs_native_api INTERFACE)
target_include_directories(bs_native_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bs_native_api INTERFACE cxx_std_20)

# Implementacoes nativas (produto). N1: arquivos do jogo sobre a pasta de recursos. N2a: relogio
# logico e fila de eventos. N3-1: nucleo de entrada (controles, mapa de acoes em TOML, XInput de reserva).
# N4: saves (formato VMS/envelope e armazenamento atomico em disco).
add_library(bs_native STATIC platform/data_files.cpp platform/clock.cpp platform/event_queue.cpp
            platform/progress.cpp platform/input_map.cpp platform/input_system.cpp
            platform/saves_format.cpp platform/saves_store.cpp platform/install_config.cpp
            platform/graphics_settings.cpp   # N7: modelo das configuracoes graficas (graficos.toml)
            platform/user_settings.cpp       # N7c: audio.toml e legendas.toml (menu interno)
            platform/camera_settings.cpp     # N7c: camera livre ([camera] em controles.toml)
            platform/edition.cpp             # N9.0: perfil de edicao (Original, Remastered)
            platform/stall_trace.cpp         # N8.8: diagnostico BS_TRAVAS (travas, esperas, fila de audio)
            platform/telemetry.cpp           # N9.5 fase 5C: telemetria de desempenho (BS_TELEMETRIA, desligada por padrao)
            platform/saves_io_${BS_PLAT}.cpp platform/xinput_pad_${BS_PLAT}.cpp)  # N9-L: backends de plataforma
target_link_libraries(bs_native PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus $<$<PLATFORM_ID:Windows>:xinput>
                      $<$<PLATFORM_ID:Windows>:psapi>)

# N5: filmes nativos (catalogo por ID com mods, leitura e gravacao MP4 por Media Foundation)
add_library(bs_native_video STATIC video/movie_catalog.cpp video/movie_playback.cpp video/subtitles.cpp video/movie_color.cpp)
# Texto sem DirectWrite (N9-L L4; nos dois sistemas desde o N9.2, decisao do mantenedor de 2026-11-04): FreeType (FTL) sobre a
# Noto Sans (OFL 1.1) embutida. So os modulos de fonte TrueType, sem zlib, PNG, bzip2, Brotli nem HarfBuzz. O menu e as
# legendas do Linux e a interface da Remastered (N9.2) desenham por aqui (native/text/text_engine.h). As legendas e o menu
# interno do Windows continuam no DirectWrite (a Original nao muda).
include(FetchContent)
foreach(_ft ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${_ft} ON CACHE BOOL "" FORCE)
endforeach()
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG        VER-2-13-3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(freetype)
set_property(TARGET freetype PROPERTY COMPILE_OPTIONS -w)  # codigo de terceiros sem alteracoes
set(BS_FONT_REGULAR  ${PROJECT_SOURCE_DIR}/third_party/fonts/noto-sans/NotoSans-Regular.ttf)
set(BS_FONT_SEMIBOLD ${PROJECT_SOURCE_DIR}/third_party/fonts/noto-sans/NotoSans-SemiBold.ttf)
if(WIN32)
  # o MSVC nao monta o .incbin: as fontes viram vetores C++ gerados na configuracao (text/fonts_embed.cmake)
  set(_bs_fonts_cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts_embed.cpp)
  add_custom_command(OUTPUT ${_bs_fonts_cpp}
    COMMAND ${CMAKE_COMMAND} -DREGULAR=${BS_FONT_REGULAR} -DSEMIBOLD=${BS_FONT_SEMIBOLD} -DOUT=${_bs_fonts_cpp}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/text/fonts_embed.cmake
    DEPENDS ${BS_FONT_REGULAR} ${BS_FONT_SEMIBOLD} ${CMAKE_CURRENT_SOURCE_DIR}/text/fonts_embed.cmake VERBATIM)
  add_library(bs_native_text STATIC text/text_freetype.cpp ${_bs_fonts_cpp})
else()
  enable_language(ASM)
  configure_file(text/fonts_embed.S.in ${CMAKE_CURRENT_BINARY_DIR}/fonts_embed.S @ONLY)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/fonts_embed.S PROPERTIES OBJECT_DEPENDS "${BS_FONT_REGULAR};${BS_FONT_SEMIBOLD}")
  add_library(bs_native_text STATIC text/text_freetype.cpp text/fonts_embed_posix.cpp ${CMAKE_CURRENT_BINARY_DIR}/fonts_embed.S)
endif()
target_link_libraries(bs_native_text PUBLIC bs_native_api PRIVATE freetype)

if(WIN32)
  target_sources(bs_native_video PRIVATE video/mf_movie.cpp video/subtitles_dwrite.cpp)
else()
  # N9-L: os SFD do disco, decodificados na hora (pl_mpeg + ADX); MP4 de mod pelo ffmpeg externo
  target_sources(bs_native_video PRIVATE video/sfd_movie.cpp video/sfd_media.cpp video/sfd_info.cpp video/pl_mpeg_impl.c
                 video/movie_decoder_posix.cpp video/ffmpeg_movie.cpp video/subtitles_text_posix.cpp)
  target_include_directories(bs_native_video PRIVATE ${PROJECT_SOURCE_DIR}/third_party/pl_mpeg)
  target_link_libraries(bs_native_video PUBLIC bs_native_text)
  set_source_files_properties(video/pl_mpeg_impl.c PROPERTIES COMPILE_OPTIONS -w)
endif()
target_link_libraries(bs_native_video PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus
                      $<$<PLATFORM_ID:Windows>:mfplat;mfreadwrite;mfuuid;ole32;d2d1;dwrite;windowscodecs>)
# N5 (D2): saida de audio (XAudio2 e nula) atras de IAudioOutput; o N6 reaproveita
# OGG/Vorbis (third_party/xiph/README.md): so a decodificacao do libogg 1.3.6 e do libvorbis 1.3.7, compilada aqui (sem
# o CMake deles; nada baixado). Codigo C de terceiros sem alteracoes: os avisos dele nao valem para o projeto.
set(BS_XIPH ${PROJECT_SOURCE_DIR}/third_party/xiph)
add_library(bs_xiph_vorbis STATIC ${BS_XIPH}/libogg/src/bitwise.c ${BS_XIPH}/libogg/src/framing.c
            ${BS_XIPH}/libvorbis/lib/mdct.c ${BS_XIPH}/libvorbis/lib/smallft.c ${BS_XIPH}/libvorbis/lib/block.c
            ${BS_XIPH}/libvorbis/lib/envelope.c ${BS_XIPH}/libvorbis/lib/window.c ${BS_XIPH}/libvorbis/lib/lsp.c
            ${BS_XIPH}/libvorbis/lib/lpc.c ${BS_XIPH}/libvorbis/lib/analysis.c ${BS_XIPH}/libvorbis/lib/synthesis.c
            ${BS_XIPH}/libvorbis/lib/psy.c ${BS_XIPH}/libvorbis/lib/info.c ${BS_XIPH}/libvorbis/lib/floor1.c
            ${BS_XIPH}/libvorbis/lib/floor0.c ${BS_XIPH}/libvorbis/lib/res0.c ${BS_XIPH}/libvorbis/lib/mapping0.c
            ${BS_XIPH}/libvorbis/lib/registry.c ${BS_XIPH}/libvorbis/lib/codebook.c ${BS_XIPH}/libvorbis/lib/sharedbook.c
            ${BS_XIPH}/libvorbis/lib/lookup.c ${BS_XIPH}/libvorbis/lib/bitrate.c ${BS_XIPH}/libvorbis/lib/vorbisfile.c)
target_include_directories(bs_xiph_vorbis PUBLIC ${BS_XIPH}/libogg/include ${BS_XIPH}/libvorbis/include
                           PRIVATE ${BS_XIPH}/libvorbis/lib)
if(MSVC)
  target_compile_options(bs_xiph_vorbis PRIVATE /W0)
  target_compile_definitions(bs_xiph_vorbis PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(bs_xiph_vorbis PRIVATE -w)
endif()

# N6: audio de gameplay portavel (ADPCM da AICA, mixer, catalogo com mods, leitura de arquivos); sem
# Win32 - o backend de saida (XAudio2 hoje, SDL3 no futuro) fica no bs_native_audio
add_library(bs_native_audio_core STATIC audio/aica_adpcm.cpp audio/aica_dsp.cpp audio/mixer.cpp audio/sound_catalog.cpp audio/sound_file.cpp
            audio/sd_adapter.cpp audio/sosb.cpp  # adaptador das 5 entradas do SDK; efeitos do BS_SND.MLT (fase C)
            audio/voice_player.cpp               # fase E: falas (MW WAV) no tempo logico, com dublagem
            audio/sound_flac.cpp                 # FLAC de mods (dr_flac, third_party/dr_libs)
            audio/sound_vorbis.cpp               # OGG/Vorbis de mods (libogg + libvorbis, third_party/xiph)
            audio/manatee.cpp                    # fase F: sequencias e bancos do Manatee (M*.MSB/MPB)
            audio/manatee_seq.cpp                # fase F: o sequenciador (a agenda do driver, 250 Hz)
            audio/manatee_synth.cpp              # fase F: o sintetizador (camadas, tom do driver, mixer)
            audio/manatee_player.cpp)            # fase F: slots, portas e faders do driver (o lado que o SDK comanda)
# so o sound_flac.cpp inclui o dr_flac; os avisos do cabecalho de terceiros nao entram como erro
target_include_directories(bs_native_audio_core PRIVATE ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
target_link_libraries(bs_native_audio_core PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus bs_xiph_vorbis)

# Localizacao (docs/LOCALIZACAO-PLANO.md): contratos portaveis de textos traduzidos, legendas novas (filmes e
# falas) e pacotes de mods; o parser SRT/WebVTT e o do N5 (bs_native_video). Sem desenho (N7) e sem ferramentas (N9).
add_library(bs_native_localization STATIC localization/locale.cpp localization/mod_packages.cpp
            localization/text_localization.cpp localization/subtitle_service.cpp localization/subtitle_presenter.cpp
            localization/dialogue_bridge.cpp)  # N6 fase E: eventos das falas -> legendas
target_link_libraries(bs_native_localization PUBLIC bs_native_api bs_native_audio_core PRIVATE bs_native_video tomlplusplus::tomlplusplus)

# N7 (preparacao): regras portaveis do renderer nativo (texturas do PVR para RGBA8, validacao e ordem
# da IR de render); sem API grafica - o backend Vulkan fica fora do jogo ate a integracao
add_library(bs_native_render STATIC render/texture_convert.cpp render/render_ir.cpp render/fidelity_scenes.cpp
            render/texture_replacement.cpp  # contrato das texturas HD futuras (sem carregamento no N7)
            render/frame_prep.cpp           # N7b: o que o rasterizador decide por triangulo, para a GPU
            render/post_shader.cpp          # N7c: pacotes e validacao dos shaders de pos-processamento
            render/wide_frame.cpp           # N9.5 fase 1: o quadro largo (hor+) por translacao da IR
            render/water.cpp                # N9.7: ondulacao so nas superficies de agua identificadas
            render/material_catalog.cpp     # N9.6 fase 2b: o catalogo de materiais por TextureId (Remastered)
            render/soft/soft_raster.cpp     # N8.7: rasterizador de referencia em software (o recuo do Vulkan)
            render/soft/soft_parallel_${BS_PLAT}.cpp  # N9-L: linhas em paralelo (backend de plataforma)
            render/soft/soft_device.cpp)    # N8.7: o renderizador em software sobre a IR
# (N8.8: o contrato do Kamui com os enderecos do guest saiu daqui para runtime/kamui, a biblioteca bs_kamui)
target_link_libraries(bs_native_render PUBLIC bs_native_api PRIVATE tomlplusplus::tomlplusplus)

# N7b: backend Vulkan (offscreen; o Vulkan e carregado em tempo de execucao, sem SDK; cabecalhos Khronos em
# third_party/vulkan, Apache-2.0; SPIR-V versionado em render/vulkan/spirv, gerado por tools/dev/gera_spirv.ps1)
add_library(bs_native_render_vk STATIC render/vulkan/vk_device.cpp render/vulkan/vk_present.cpp
            render/vulkan/vk_post.cpp)
target_include_directories(bs_native_render_vk PRIVATE ${PROJECT_SOURCE_DIR}/third_party/vulkan)
target_link_libraries(bs_native_render_vk PUBLIC bs_native_render PRIVATE bs_native ${CMAKE_DL_LIBS})

add_library(bs_native_audio STATIC audio/audio_output.cpp)
if(WIN32)
  target_sources(bs_native_audio PRIVATE audio/audio_output_xaudio2.cpp)
else()
  target_sources(bs_native_audio PRIVATE audio/audio_output_posix.cpp)
endif()
target_link_libraries(bs_native_audio PUBLIC bs_native_api PRIVATE bs_native $<$<PLATFORM_ID:Windows>:xaudio2;ole32>)

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  # Compila todos os cabecalhos so com native/ no include path: nada do legado pode vazar.
  add_executable(bs_native_api_check ${CMAKE_SOURCE_DIR}/tests/n0/native_api_check.cpp)
  target_link_libraries(bs_native_api_check PRIVATE bs_native)
endif()
