# N9.2 (Windows): as fontes do texto (Noto Sans, OFL 1.1) como vetores C++, para o MSVC, que nao monta o .incbin do
# fonts_embed.S.in do Linux. Chamado na compilacao (native/CMakeLists.txt):
#   cmake -DREGULAR=<ttf> -DSEMIBOLD=<ttf> -DOUT=<fonts_embed.cpp> -P fonts_embed.cmake
function(bs_font_array var path out)
  file(READ "${path}" hex HEX)
  string(LENGTH "${hex}" n)
  math(EXPR bytes "${n} / 2")
  # 0xNN, por byte; uma quebra de linha a cada 32 bytes (o MSVC aceita linhas longas, mas o arquivo fica legivel)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
  string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" body "${body}")
  set(${out} "alignas(16) const unsigned char ${var}[${bytes}] = {\n${body}\n};\nconst std::size_t ${var}_size = ${bytes};\n" PARENT_SCOPE)
endfunction()

bs_font_array(bs_font_noto_regular_data "${REGULAR}" reg)
bs_font_array(bs_font_noto_semibold_data "${SEMIBOLD}" semi)
file(WRITE "${OUT}.tmp" "// Gerado por native/text/fonts_embed.cmake a partir de third_party/fonts/noto-sans (OFL 1.1). Nao editar.\n"
  "#include \"text/fonts_embed.h\"\n\n#include <cstddef>\n\nnamespace {\n${reg}${semi}}\n\n"
  "namespace bs::native::text {\nFontBlob embedded_font(int i) {\n"
  "    if (i == 0) return {bs_font_noto_regular_data, bs_font_noto_regular_data_size};\n"
  "    if (i == 1) return {bs_font_noto_semibold_data, bs_font_noto_semibold_data_size};\n"
  "    return {};\n}\n}\n")
file(RENAME "${OUT}.tmp" "${OUT}")
