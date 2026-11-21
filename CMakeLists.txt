cmake_minimum_required(VERSION 3.25)
project(BlueStingerRecompiled LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BS_BUILD_TESTS "Compilar testes" ON)
option(BS_BUILD_GAME  "Compilar o executavel do jogo (requer generated/)" OFF)
# OFF no kit do Builder: so runtime + jogo (sem tradutor, ferramentas de dev nem rede)
option(BS_BUILD_TOOLS "Compilar tradutor, ferramentas de desenvolvimento e o Builder" ON)
# Projeto editavel (mods): chamadas entre funcoes geradas passam por slots substituiveis e
# os .cpp de BS_MODS_DIR entram no executavel (runtime/mods/mod_api.h).
option(BS_MODS "Compilar com a camada de mods" OFF)
# N2b (diagnostico de investigacao, nunca o produto): sonda no caminho de escrita (cpu/write_watch.h)
option(BS_WRITE_WATCH "Diagnostico: registro de efeitos de escrita" OFF)
if(BS_WRITE_WATCH)
  add_compile_definitions(BS_WRITE_WATCH=1)
endif()
# N8.2 (D-N8-3): build de validacao. So nele entram as mutacoes e sondas exclusivas de teste (ruido de ciclos, sonda de
# hardware do N0, diagnostico do CRI: runtime/debug); no produto e no Builder elas viram recusa explicita
# (runtime/debug/validacao_stubs.cpp). A instrumentacao das referencias e o tempo estavel ficam nos dois (mantenedor,
# 2026-10-26; docs/N8-2-SEPARACAO.md).
option(BS_VALIDACAO "Build de validacao (N8.2): mutacoes e sondas exclusivas de teste" OFF)
if(BS_WRITE_WATCH AND NOT BS_VALIDACAO)
  message(FATAL_ERROR "BS_WRITE_WATCH e diagnostico de investigacao: so com -DBS_VALIDACAO=ON")
endif()
if(BS_VALIDACAO)
  add_compile_definitions(BS_VALIDACAO=1)
endif()
# PAL P2 (docs/PAL-PLANO.md, arquitetura A): enderecos do jogo por versao, escolhidos na compilacao. O codigo nativo
# cita o endereco americano em bs::jogo(...) (runtime/versao/include/versao/enderecos.h); a pasta fica visivel a todos os
# alvos, inclusive os que nao enxergam runtime/. 0 = americano (padrao: nenhuma definicao, o executavel nao muda),
# 1 = PAL alemao (tabela enderecos_pal_de.inc gerada por tools/dev/versao_traduz.py --inc).
# Projeto exportado (PAL P8): config/versao_jogo.txt, gravado pelo Builder, da o padrao (o C++ gerado e daquela versao)
set(_bs_versao_padrao 0)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/versao_jogo.txt")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/config/versao_jogo.txt" _bs_versao_padrao LIMIT_COUNT 1)
endif()
set(BS_VERSAO_JOGO ${_bs_versao_padrao} CACHE STRING "Versao do jogo: 0 = americano (T13001N), 1 = PAL alemao (T13001D 18), 2 = PAL ingles (T13001D-05), 3 = japones (HDR-0003 V1.006), 4 = Director's Cut EN, 5 = Director's Cut FR, 6 = PAL frances (T13001D 09)")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/versao_jogo.txt" AND NOT BS_VERSAO_JOGO STREQUAL _bs_versao_padrao)
  # o C++ gerado do projeto e de uma versao so: o arquivo vale sobre um valor antigo no cache (revisao Codex 2026-11-19:
  # um projeto reexportado de outra versao na mesma pasta de build manteria o BS_VERSAO_JOGO anterior)
  message(STATUS "BS_VERSAO_JOGO: ${BS_VERSAO_JOGO} -> ${_bs_versao_padrao} (config/versao_jogo.txt)")
  set(BS_VERSAO_JOGO ${_bs_versao_padrao} CACHE STRING "Versao do jogo (config/versao_jogo.txt)" FORCE)
endif()
if(NOT BS_VERSAO_JOGO MATCHES "^[0-6]$")
  message(FATAL_ERROR "BS_VERSAO_JOGO: 0 a 6 (runtime/versao/include/versao/enderecos.h; recebido: ${BS_VERSAO_JOGO})")
endif()
# a tabela de enderecos da versao tem de existir (revisao Codex 2026-11-19: 2 a 5 sao numerados, mas as tabelas chegam
# com o suporte de cada uma; sem ela, o erro sai aqui e nao no meio da compilacao)
set(_bs_tabelas "-" "enderecos_pal_de.inc" "enderecos_pal_en.inc" "enderecos_jp.inc" "enderecos_jp.inc" "enderecos_jp.inc" "enderecos_pal_fr.inc")
list(GET _bs_tabelas ${BS_VERSAO_JOGO} _bs_tabela)
if(NOT _bs_tabela STREQUAL "-" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/versao/include/versao/${_bs_tabela}")
  message(FATAL_ERROR "BS_VERSAO_JOGO=${BS_VERSAO_JOGO}: a versao ainda nao tem suporte (falta runtime/versao/include/versao/${_bs_tabela}, gerada por tools/dev/versao_traduz.py --inc)")
endif()
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/runtime/versao/include")
if(NOT BS_VERSAO_JOGO STREQUAL "0")
  add_compile_definitions(BS_VERSAO_JOGO=${BS_VERSAO_JOGO})
endif()
set(BS_MODS_DIR "" CACHE PATH "Pasta com os mods (.cpp) do projeto editavel")
# N9-L: sufixo dos backends de plataforma (<nome>_win.cpp, <nome>_posix.cpp)
if(WIN32)
  set(BS_PLAT win)
else()
  set(BS_PLAT posix)
endif()
# bibliotecas de importacao do XInput e do XAudio2: no MSVC, xinput.lib e xaudio2.lib (XInput1_4.dll e XAudio2_9.dll); no
# compilador portatil (Zig/MinGW) elas tem o nome da DLL
if(MSVC)
  set(BS_LIB_XINPUT xinput)
  set(BS_LIB_XAUDIO2 xaudio2)
else()
  set(BS_LIB_XINPUT xinput1_4)
  set(BS_LIB_XAUDIO2 xaudio2_9)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  # Ninja acha as dependencias de cabecalho pelo prefixo do /showIncludes, que o cl traduz.
  # Com o prefixo localizado, a codificacao muda com a pagina de codigo do terminal e um
  # build de outro terminal perde as dependencias (objetos velhos: STATUS.md, N1). Os
  # presets pedem ingles (VSLANG=1033); sem o pacote de idioma ingles do VS, compile sempre
  # do mesmo tipo de terminal. O teste n2_build_incremental confere.
  # BS_MSVC_DEPS_JSON (D10, ligado por padrao desde o N2b): as dependencias saem do /sourceDependencies (JSON, sem
  # texto traduzido) e tools/build/msvc_srcdeps.cmake as converte para o ninja. Independe do
  # idioma do MSVC e da pagina de codigo do terminal (teste n2_build_incremental).
  option(BS_MSVC_DEPS_JSON "Dependencias de cabecalho do MSVC por /sourceDependencies" ON)
  if(BS_MSVC_DEPS_JSON AND CMAKE_GENERATOR MATCHES "Ninja")
    foreach(lang C CXX)
      set(CMAKE_${lang}_DEPFILE_FORMAT gcc)
      set(CMAKE_DEPFILE_FLAGS_${lang} "/sourceDependencies <DEP_FILE>.json")
      set(CMAKE_${lang}_COMPILER_LAUNCHER "${CMAKE_COMMAND};-P;${CMAKE_SOURCE_DIR}/tools/build/msvc_srcdeps.cmake;--")
    endforeach()
  elseif(CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CL_SHOWINCLUDES_PREFIX AND
     NOT CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^[ -~]*$")
    message(WARNING "prefixo /showIncludes localizado (\"${CMAKE_CL_SHOWINCLUDES_PREFIX}\"): compile sempre "
                    "do mesmo terminal (pagina de codigo) ou instale o pacote de idioma ingles do Visual Studio")
  endif()
else()
  add_compile_options(-Wall -Wextra)
  # N9-L (docs/N9-L-PLANO.md secao 3): o codigo do SH-4 reinterpreta memoria, conta com estouro em complemento de dois e
  # precisa de ponto flutuante sem fusao de operacoes. Sem isto a Original deixa de ser identica
  add_compile_options(-fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math)
endif()

# toml++ (MIT): configuracao do tradutor e, desde o N3-1, o arquivo de controles do produto. No kit do
# Builder as fontes vem em third_party/tomlplusplus (FETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS, sem rede).
include(FetchContent)
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(tomlplusplus)

add_subdirectory(tools/extractor)
add_subdirectory(tools/dcmap)
if(BS_BUILD_TOOLS)
  add_subdirectory(translator)
endif()
add_subdirectory(runtime)
# N0: alvos do port nativo (docs/PLANO-PORT-NATIVO.md, docs/N0-RELATORIO.md)
add_subdirectory(native)
add_subdirectory(legacy)
add_subdirectory(transitional)

# Codigo gerado por bs-recomp a partir do executavel do usuario (nao versionado).
set(BS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH "Saida do bs-recomp")
if(EXISTS "${BS_GENERATED_DIR}/generated.cmake")
  include("${BS_GENERATED_DIR}/generated.cmake")
  add_library(bs_generated STATIC ${BS_GENERATED_SOURCES})
  target_include_directories(bs_generated PUBLIC "${BS_GENERATED_DIR}")
  target_link_libraries(bs_generated PUBLIC bs_runtime)
  if(BS_MODS)
    target_compile_definitions(bs_generated PUBLIC BS_MODS=1)
  endif()
  if(MSVC)
    # substitui as opcoes herdadas (/W4) para nao gerar o aviso D9025
    set_property(TARGET bs_generated PROPERTY COMPILE_OPTIONS /bigobj /W1 /utf-8 /permissive-)
  else()
    target_compile_options(bs_generated PRIVATE -w)
  endif()
  if(BS_BUILD_TOOLS)
  add_executable(bs-gen-check tools/gen_check/main.cpp)
  target_link_libraries(bs-gen-check PRIVATE bs_generated)
  add_executable(bs-diff tools/diff/main.cpp)
  target_link_libraries(bs-diff PRIVATE bs_generated bs_runtime_cdi)
  endif()
endif()

# Executavel nativo e seu nucleo (app/); o executavel so com BS_BUILD_GAME=ON.
add_subdirectory(app)

# N9.5 fase 0 (so investigacao): desenha uma IR guardada numa largura escolhida e mede a geometria
if(BS_BUILD_TOOLS AND BS_VALIDACAO)
  add_executable(bs-ir-tela tools/ir_tela/main.cpp tools/ir_tela/geometria.cpp)  # geometria: N9.6 fase 0
  target_link_libraries(bs-ir-tela PRIVATE bs_transitional)
endif()

# Builder (CDI -> BlueStinger.exe na maquina do usuario); no Linux desde a L5 do N9-L (so o bs-builder)
if(BS_BUILD_TOOLS)
  add_subdirectory(builder)
endif()

if(BS_BUILD_TESTS AND BS_BUILD_TOOLS)
  enable_testing()
  add_subdirectory(tests)
  # Regressao do boot contra o trace do Flycast (dados locais em BS_SNAPSHOT_DIR; senao SKIP).
  find_package(Python3 COMPONENTS Interpreter)
  if(TARGET bs-diff AND Python3_FOUND)
    add_test(NAME boot_trace
             COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tests/diff/boot_regression.py"
                     $<TARGET_FILE:bs-diff> "${CMAKE_SOURCE_DIR}/tools/dev/flycast_oracle.py"
                     "${CMAKE_SOURCE_DIR}/tests/diff/boot_accepted.tsv")
    # raiz do repositorio: BS_DISC_IMAGE/BS_SNAPSHOT_DIR relativos funcionam
    set_tests_properties(boot_trace PROPERTIES SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                          TIMEOUT 3600)  # oraculo Flycast: duracao nao medida aqui, limite so contra travamento
  endif()
endif()
