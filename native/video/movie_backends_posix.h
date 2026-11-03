#pragma once
// N9-L (L4): os decodificadores de filme do Linux, escolhidos pelo conteudo em movie_decoder_posix.cpp.
#include "video/movie_decoder.h"

#include <filesystem>
#include <memory>
#include <span>

namespace bs::native::video {

// SFD do disco (Sofdec: MPEG-1 + ADX) e MPEG-1 Program Stream: pl_mpeg e o ADX proprio (sfd_movie.cpp)
std::unique_ptr<IMovieDecoder> make_sfd_decoder();
// Qualquer formato que o ffmpeg do sistema leia (MP4 de mod), por processos separados (ffmpeg_movie.cpp). `timeoutMs`: prazo
// sem nenhum byte de um filho antes de encerra-lo como travado (0 = o padrao de 10 s; os testes usam um prazo curto)
std::unique_ptr<IMovieDecoder> make_ffmpeg_decoder(int timeoutMs = 0);
// `ffmpeg` e `ffprobe` executaveis no PATH (procurados a cada chamada: o PATH pode mudar)
bool ffmpeg_available();
// a sonda real, nao so a presenca do programa: o ffprobe le o formato e o ffmpeg decodifica o primeiro quadro do video
bool ffmpeg_can_read(const std::filesystem::path& file, std::string* err = nullptr);
// MPEG Program Stream que o leitor do SFD toca: a identificacao do arquivo inteiro (sfd_identify: video MPEG-1 e, se houver
// audio, ADX), a mesma conferencia do leitor ao abrir. Um MPEG-PS comum com audio MPEG nao e, e vai para o ffmpeg.
bool sfd_reader_fits(const std::filesystem::path& file);

enum class MovieContainer : uint8_t { MpegProgramStream, Other, Unreadable };
// Pelos primeiros bytes: "Sofdec Stream" ou pack MPEG (000001BA) no inicio
MovieContainer sniff_movie(const std::filesystem::path& file);
MovieContainer sniff_movie(std::span<const uint8_t> head);

} // namespace bs::native::video
