#pragma once
// N9-L: as operacoes de arquivo da escrita atomica dos saves (saves_store.cpp), por plataforma. A sequencia e a recuperacao ficam
// em saves_store.cpp e sao as mesmas nos dois sistemas.
#include "platform/saves.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace bs::native::saves::io {

bool is_read_only(const std::filesystem::path& p);
// Grava os `n` primeiros bytes e forca a ida ao disco. None so com o arquivo inteiro (n == data.size()) gravado.
// `fail_flush`: falha injetada no passo de forcar a gravacao.
SaveError write_durable(const std::filesystem::path& p, const std::vector<std::byte>& data, size_t n, bool fail_flush);
// tmp vira dst; dst nao existe.
bool move_new(const std::filesystem::path& tmp, const std::filesystem::path& dst);
// tmp vira dst e o dst anterior vira bak.
bool replace_keep_backup(const std::filesystem::path& dst, const std::filesystem::path& tmp, const std::filesystem::path& bak);
// tmp vira dst, existindo ou nao.
bool move_replace(const std::filesystem::path& tmp, const std::filesystem::path& dst);

} // namespace bs::native::saves::io
