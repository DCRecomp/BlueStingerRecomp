# N8.2: dados do jogo para o produto (bs_disc_data: RuntimeDisc e a conferencia dos dados de execucao, IP.BIN,
# descrambler, ISO9660 sobre a interface generica de disco, hash) e leitura de imagens para as ferramentas (bs_disc:
# leitor de CDI e o extrator). O executavel do jogo liga so bs_disc_data (catraca n8_produto_papeis).
add_library(bs_disc_data STATIC
  disc/disc_image.cpp
  disc/iso9660.cpp
  disc/ip_bin.cpp
  disc/scramble.cpp
  util/hash.cpp
  runtime_data.cpp
  boot_identity.cpp
)
target_include_directories(bs_disc_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_definitions(bs_disc_data PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

add_library(bs_disc STATIC
  disc/cdi_image.cpp
  disc/track_files_image.cpp
  disc/disc_open.cpp
  extract.cpp
)
target_link_libraries(bs_disc PUBLIC bs_disc_data)

# I6 e I8 (docs/IMAGENS-DISCO-PLANO.md): imagens CHD pela libchdr (BSD-3) e pacotes .zip (o miniz da libchdr, MIT) e .7z
# (LZMA SDK 26.02, so os arquivos em dominio publico). So nas ferramentas: o kit que compila o jogo na maquina do jogador
# (BS_BUILD_TOOLS=OFF) nao baixa nem compila nada disto, e o jogo nunca le a imagem. Versoes fixas pelo SHA-256.
if(BS_BUILD_TOOLS)
  include(FetchContent)
  set(MINIZ_ARCHIVE_APIS ON CACHE BOOL "" FORCE)  # leitura de .zip
  set(MINIZ_DEFLATE_APIS ON CACHE BOOL "" FORCE)  # so o teste unitario grava um .zip
  set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
  set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
  set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "" FORCE)
  set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_Declare(libchdr
    URL https://github.com/rtissera/libchdr/archive/47d794151b2b64f6aba60304e6480f4f5a2bc24b.tar.gz
    URL_HASH SHA256=ee1131e748a2c7d7559b7054540ebe49637433901f92071560df52e369f7c7af)
  FetchContent_MakeAvailable(libchdr)
  # a copia do dr_flac da libchdr fica interna a ela (o audio do jogo tem a sua, em third_party/dr_libs)
  target_compile_definitions(chdr-static PRIVATE DRFLAC_API=static)
  FetchContent_Declare(lzmasdk
    URL https://github.com/ip7z/7zip/archive/refs/tags/26.02.tar.gz
    URL_HASH SHA256=7e5600ad0b21918bc41803bd84d0c2967e097fb7fbaa2e7aeb90beeb1121e54b
    SOURCE_SUBDIR sem-cmake)  # so as fontes C; nada do 7-Zip e configurado
  FetchContent_MakeAvailable(lzmasdk)
  set(_bs_7z ${lzmasdk_SOURCE_DIR}/C)
  # o LzmaDec da libchdr tem os simbolos renomeados (CHDR_*): o do .7z e este, sem conflito
  add_library(bs_7z STATIC
    ${_bs_7z}/7zAlloc.c ${_bs_7z}/7zArcIn.c ${_bs_7z}/7zBuf.c ${_bs_7z}/7zCrc.c ${_bs_7z}/7zCrcOpt.c ${_bs_7z}/7zDec.c
    ${_bs_7z}/7zFile.c ${_bs_7z}/7zStream.c ${_bs_7z}/Bcj2.c ${_bs_7z}/Bra.c ${_bs_7z}/Bra86.c ${_bs_7z}/BraIA64.c
    ${_bs_7z}/CpuArch.c ${_bs_7z}/Delta.c ${_bs_7z}/Lzma2Dec.c ${_bs_7z}/LzmaDec.c ${_bs_7z}/Ppmd7.c ${_bs_7z}/Ppmd7Dec.c)
  target_include_directories(bs_7z PUBLIC ${_bs_7z})
  if(MSVC)
    target_compile_options(bs_7z PRIVATE /W0)
  endif()
  target_sources(bs_disc PRIVATE disc/chd_image.cpp disc/archive.cpp)
  target_include_directories(miniz PUBLIC ${libchdr_SOURCE_DIR}/deps/miniz-3.1.2)  # o alvo da libchdr nao exporta
  target_link_libraries(bs_disc PRIVATE chdr-static miniz bs_7z)
  target_compile_definitions(bs_disc PUBLIC BS_DISC_CHD=1)
endif()

add_executable(bs-extract main.cpp)
target_link_libraries(bs-extract PRIVATE bs_disc)
