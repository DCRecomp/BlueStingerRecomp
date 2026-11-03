"""N9-L (L4, desenvolvimento): a imagem dos SFD do disco por duas decodificacoes independentes do MPEG-1 (V3 do N5): o pl_mpeg
do jogo (pelo BS_PROBE_RAW do bs-video-probe, RGB24) e o ffmpeg. O YUV do ffmpeg vira RGB pela mesma conta inteira do pl_mpeg
(plm_frame_to_rgb: BT.601 em faixa de TV, croma repetido em 2x2), entao a diferenca medida e so a da decodificacao (IDCT e
arredondamentos), sem a da conversao de cor. O MPEG-1 nao exige IDCT exata (IEEE 1180): o esperado e muito perto, nao igual.

Falha (revisao Codex 20261103-030426) se, em algum filme: o numero de quadros difere, o ffmpeg sai com erro, o PSNR minimo fica
abaixo de 42 dB, a diferenca media passa de 1,0 nivel ou a maxima passa de 32 niveis. Limites fixados em 2026-11-03 sobre a
primeira medida (pior caso: PSNR minimo 45,15 dB, media 0,64, maxima 15).

    python tools/dev/sfd_oraculo.py <pasta BSDATA> <bs-video-probe.exe> <saida.tsv> [--ffmpeg ffmpeg]
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np


def pl_rgb(y, u, v, w, h):
    """plm_frame_to_rgb sobre planos I420 (uint8)"""
    y = y.astype(np.int32).reshape(h, w)
    cb = np.repeat(np.repeat(u.astype(np.int32).reshape(h // 2, w // 2) - 128, 2, 0), 2, 1)
    cr = np.repeat(np.repeat(v.astype(np.int32).reshape(h // 2, w // 2) - 128, 2, 0), 2, 1)
    yy = ((y - 16) * 76309) >> 16
    r = (cr * 104597) >> 16
    g = (cb * 25674 + cr * 53278) >> 16
    b = (cb * 132201) >> 16
    return np.clip(np.stack([yy + r, yy - g, yy + b], -1), 0, 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bsdata")
    ap.add_argument("probe")
    ap.add_argument("saida")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    a = ap.parse_args()
    linhas = ["filme\tquadros_pl_mpeg\tquadros_ffmpeg\tpixels_iguais_pct\tdif_media\tdif_max\tpsnr_min_db\tpsnr_med_db\tresultado"]
    falhas = 0
    for nome in sorted(f for f in os.listdir(a.bsdata) if f.upper().endswith(".SFD")):
        sfd = os.path.join(a.bsdata, nome)
        with tempfile.TemporaryDirectory() as tmp:
            raw = os.path.join(tmp, "pl.rgb")
            env = dict(os.environ, BS_PROBE_RAW=raw)
            subprocess.run([a.probe, sfd], env=env, check=True, stdout=subprocess.DEVNULL)
            w, h = 320, 240
            fs = w * h * 3
            n_pl = os.path.getsize(raw) // fs
            pl = open(raw, "rb")
            # passthrough: os quadros decodificados, sem duplicar nem descartar pelos timestamps do Program Stream
            ff = subprocess.Popen([a.ffmpeg, "-v", "error", "-i", sfd, "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "rawvideo",
                                   "-pix_fmt", "yuv420p", "-"],
                                  stdout=subprocess.PIPE)
            ys, cs = w * h, (w // 2) * (h // 2)
            n_ff, iguais, soma, dmax, psnrs = 0, 0, 0.0, 0, []
            while True:
                buf = ff.stdout.read(ys + 2 * cs)
                if len(buf) < ys + 2 * cs:
                    break
                fr = np.frombuffer(buf, np.uint8)
                rgb = pl_rgb(fr[:ys], fr[ys:ys + cs], fr[ys + cs:], w, h)
                if n_ff < n_pl:
                    ref = np.frombuffer(pl.read(fs), np.uint8).reshape(h, w, 3)
                    d = np.abs(rgb.astype(np.int16) - ref.astype(np.int16))
                    iguais += int((d.max(-1) == 0).sum())
                    soma += float(d.mean())
                    dmax = max(dmax, int(d.max()))
                    mse = float((d.astype(np.float64) ** 2).mean())
                    psnrs.append(99.0 if mse == 0 else 10 * np.log10(255 * 255 / mse))
                n_ff += 1
            rc = ff.wait()
            pl.close()
        n = max(1, min(n_pl, n_ff))
        pct = 100.0 * iguais / (n * w * h)
        pmin = min(psnrs) if psnrs else 0.0
        ok = n_pl == n_ff and rc == 0 and pmin >= 42.0 and soma / n <= 1.0 and dmax <= 32
        linha = f"{nome}\t{n_pl}\t{n_ff}\t{pct:.3f}\t{soma / n:.4f}\t{dmax}\t{pmin:.2f}\t{np.mean(psnrs) if psnrs else 0:.2f}\t{'ok' if ok else 'FALHA'}"
        print(linha, flush=True)
        linhas.append(linha)
        falhas += not ok
    with open(a.saida, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(linhas) + "\n")
    print(f"{len(linhas) - 1} filmes, {falhas} falhas")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
