/* N5 (D1): implementacao do pl_mpeg (third_party/pl_mpeg, MIT, commit fixo no README de la). */
#include <stdio.h>
#include <stdlib.h>

/* N9-L (L4, achado do AddressSanitizer com SFD corrompido): a conferencia `max_address` de plm_video_process_macroblock nao conta
   a linha a mais da interpolacao vertical de meio pixel. Com o vetor de movimento apontando para o fundo do quadro, um fluxo
   corrompido le ate uma linha (e 1 byte) alem do plano, e no ultimo plano do bloco frames_data isso passa do fim da alocacao.
   Num fluxo valido nao acontece (o MPEG-1 nao deixa o vetor sair da imagem). Toda alocacao do pl_mpeg ganha uma folga zerada no
   fim, maior que a maior linha possivel (4096 pixels de luma): a leitura a mais cai dentro dela e o arquivo corrompido so mostra
   lixo, sem erro de memoria. Sem alterar o pl_mpeg.h (o pl_mpeg aceita o alocador por estas macros). */
#include <stdint.h>
#include <string.h>
#define BS_PLM_FOLGA 8192
static void *bs_plm_malloc(size_t n) { return n > SIZE_MAX - BS_PLM_FOLGA ? NULL : calloc(1, n + BS_PLM_FOLGA); }
static void *bs_plm_realloc(void *p, size_t n) {  /* a folga volta a ser zerada (revisao Codex 20261103-034502) */
	if (n > SIZE_MAX - BS_PLM_FOLGA) return NULL;
	unsigned char *q = (unsigned char *)realloc(p, n + BS_PLM_FOLGA);
	if (q) memset(q + n, 0, BS_PLM_FOLGA);
	return q;
}
#define PLM_MALLOC(sz) bs_plm_malloc(sz)
#define PLM_FREE(p) free(p)
#define PLM_REALLOC(p, sz) bs_plm_realloc(p, sz)

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Fim do stream com o ES inteiro em memoria (o caso do Builder). Dois defeitos do pl_mpeg, conferidos
   contra o ffmpeg nos SFD do disco (faltavam os 2 ultimos quadros):
   1. plm_buffer_discard_read_bytes encolhe `length`, mas o fim so e declarado com length == total_size:
      num buffer de memoria isso nunca acontece, e a ultima imagem fica esperando um proximo codigo de
      inicio. Aqui o fim e declarado (todo o ES ja esta no buffer) e a decodificacao continua;
   2. a referencia pendente so sai se a ultima imagem for I/P; se o filme terminar em B, ela e
      devolvida aqui.
   Chamado depois de plm_video_decode devolver NULL; devolve NULL quando nao ha mais quadros. Sem
   alterar o pl_mpeg.h. */
plm_frame_t *bs_plm_video_flush(plm_video_t *self) {
	if (!self->buffer->has_ended) {
		self->buffer->has_ended = TRUE;
		plm_frame_t *f = plm_video_decode(self);
		if (f) return f;
	}
	if (!self->has_reference_frame || self->assume_no_b_frames) return NULL;
	self->has_reference_frame = FALSE;
	self->frame_backward.time = self->time;
	return &self->frame_backward;
}
