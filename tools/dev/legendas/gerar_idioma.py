"""Legendas em outros idiomas (2026-11-24: es-ES, fr-FR, ru-RU, ja-JP, zh-CN): o pacote legendas-<idioma> a partir do pacote
ingles revisado e de uma traducao numerada.

Os tempos, as chaves (voz/<nome> ou filme/<nome>, linha) e o que fica de fora vem do ingles (fontes/transcricoes.en-US.tsv do
pacote legendas-en): a traducao so troca o texto. A traducao e um TSV "n<TAB>texto", uma linha por linha inglesa que vai para o
jogo, na ordem do transcricoes.en-US.tsv (sem as excluidas). Falta ou sobra de linha e erro.

Quebra das linhas: ate 2 linhas de largura 42 (um ideograma CJK conta 2). O latim e o cirilico quebram nas palavras; o chines
e o japones em qualquer caractere, de preferencia depois da pontuacao. O que nao cabe em 2 linhas vira mais de uma legenda
seguida, com o tempo da linha inglesa dividido pelo tamanho de cada parte.

Saida: <saida>/legendas-<id>
  mod.toml, README.md
  legendas/falas.<IDIOMA>.toml        as falas do gameplay
  legendas/<FILME>.<IDIOMA>.srt       os filmes (SubtitleService)
  video/<FILME>.<idioma>.srt          os filmes no leiaute do N5 (mods\\video; o player procura o idioma e depois a lingua)
  fontes/traducao.<IDIOMA>.tsv        ingles e traducao lado a lado, com o estado da linha inglesa

    python tools/dev/legendas/gerar_idioma.py --idioma es-ES --traducao es-ES.tsv --en <legendas-en>/fontes/transcricoes.en-US.tsv
        --saida mods-padrao [--versao 0.1.0]
"""
import argparse
import os
import shutil
import sys
import unicodedata

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import gerar_mod as gm  # noqa: E402  (ler_tsv, toml_str, srt_tempo, t_inicio, t_fim)

LARGURA, LINHAS = 42, 2
IDIOMAS = {  # id do pacote, nome no proprio idioma, nome em portugues, CJK
    "es-ES": ("legendas-es", "Subtítulos en español (diálogos y películas)", "espanhol", False),
    "fr-FR": ("legendas-fr", "Sous-titres en français (dialogues et films)", "francês", False),
    "ru-RU": ("legendas-ru", "Русские субтитры (диалоги и ролики)", "russo", False),
    "ja-JP": ("legendas-ja", "日本語字幕（会話とムービー）", "japonês", True),
    "zh-CN": ("legendas-zh-cn", "简体中文字幕（对话与过场动画）", "chinês simplificado", True),
}
JOGOS = ["T13001N V1.000", "T13001D V1.000", "T13001D-05 V1.000", "T13001D 09 V1.000"]
PONT_CJK = "。！？、，…）」』：；"


def largura(s):
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


def linhas_latim(texto, maximo=LARGURA):
    """quebra gulosa nas palavras (uma palavra maior que a linha fica sozinha nela)"""
    out, atual = [], ""
    for w in texto.split():
        t = (atual + " " + w).strip()
        if atual and largura(t) > maximo:
            out.append(atual)
            atual = w
        else:
            atual = t
    if atual:
        out.append(atual)
    return out


def linhas_cjk(texto, maximo=LARGURA):
    """quebra por caracteres; recua o corte para depois da pontuacao mais proxima, se houver na linha"""
    out, resto = [], texto.strip()
    while resto:
        if largura(resto) <= maximo:
            out.append(resto)
            break
        k, w = 0, 0
        while k < len(resto) and w + largura(resto[k]) <= maximo:
            w += largura(resto[k])
            k += 1
        corte = max((j + 1 for j in range(k) if resto[j] in PONT_CJK or resto[j] == " "), default=k)
        if corte < k // 2:
            corte = k
        out.append(resto[:corte].strip())
        resto = resto[corte:].strip()
    return out


def equilibrar(par, cjk):
    """2 linhas: move o corte para deixar as duas parecidas (so no latim; no CJK mantem a pontuacao)"""
    if cjk or len(par) != 2:
        return par
    palavras = (par[0] + " " + par[1]).split()
    melhor = par
    for k in range(1, len(palavras)):
        a, b = " ".join(palavras[:k]), " ".join(palavras[k:])
        if largura(a) <= LARGURA and largura(b) <= LARGURA:
            if abs(largura(a) - largura(b)) < abs(largura(melhor[0]) - largura(melhor[1])):
                melhor = [a, b]
    return melhor


def partes(texto, cjk):
    """o texto em blocos de ate 2 linhas (cada bloco e uma legenda)"""
    texto = " ".join(texto.replace("\\n", " ").split()) if not cjk else texto.replace("\\n", "").strip()
    ls = linhas_cjk(texto) if cjk else linhas_latim(texto)
    blocos = [ls[k:k + LINHAS] for k in range(0, len(ls), LINHAS)]
    return ["\n".join(equilibrar(b, cjk)) for b in blocos]


def dividir_tempo(inicio, fim, blocos):
    """o intervalo da linha inglesa dividido pelo tamanho de cada bloco"""
    pesos = [max(1, largura(b)) for b in blocos]
    total, t, out = sum(pesos), inicio, []
    for p, b in zip(pesos, blocos):
        d = (fim - inicio) * p / total
        out.append((t, t + d, b))
        t += d
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idioma", required=True, choices=sorted(IDIOMAS))
    ap.add_argument("--traducao", required=True)
    ap.add_argument("--en", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--versao", default="0.1.0")
    a = ap.parse_args()
    idf, nome, nome_pt, cjk = IDIOMAS[a.idioma]
    en = [r for r in gm.ler_tsv(a.en) if r["estado"] != "excluida"]
    tr = {}
    for l in open(a.traducao, encoding="utf-8"):
        if l.strip():
            n, texto = l.rstrip("\n").split("\t", 1)
            tr[int(n)] = texto.strip()
    if sorted(tr) != list(range(1, len(en) + 1)):
        raise SystemExit("traducao com %d linhas; o ingles tem %d" % (len(tr), len(en)))
    vazias = [n for n, t in tr.items() if not t]
    if vazias:
        raise SystemExit("linhas vazias: %s" % vazias[:20])
    mod = os.path.join(a.saida, idf)
    if os.path.isdir(mod):
        shutil.rmtree(mod)
    for d in ("legendas", "video", "fontes"):
        os.makedirs(os.path.join(mod, d))
    falas, filmes, lado, divididas = [], {}, [], 0
    for k, r in enumerate(en, 1):
        blocos = partes(tr[k], cjk)
        divididas += len(blocos) > 1
        cues = dividir_tempo(float(r["inicio"]), float(r["fim"]), blocos)
        lado.append([r["recurso"], r["linha"], r["estado"], r["texto"], tr[k]])
        for ini, fim, b in cues:
            l = dict(inicio=ini, fim=fim, texto=b)
            if r["recurso"].startswith("voz/"):
                falas.append((r["recurso"], r["id"], r["linha"], l))
            else:
                filmes.setdefault(r["arquivo"], []).append(l)
    with open(os.path.join(mod, "legendas", f"falas.{a.idioma}.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"# Legendas em {nome_pt} das falas do Blue Stinger (pacote {idf} {a.versao}).\n"
                "# Traducao por IA do pacote legendas-en revisado, sem revisao humana; os tempos sao os do ingles.\n")
        for voz, vid, k, l in falas:
            o.write(f"\n[[fala]]  # id {vid}, linha {k}\nvoz = {gm.toml_str(voz)}\ninicio = {gm.t_inicio(l['inicio']):.2f}\n"
                    f"fim = {gm.t_fim(l['fim']):.2f}\ntexto = {gm.toml_str(l['texto'])}\n")
    for nome_f, ls in filmes.items():
        srt = "".join(f"{k}\n{gm.srt_tempo(l['inicio'])} --> {gm.srt_tempo(l['fim'])}\n{l['texto']}\n\n" for k, l in enumerate(ls, 1))
        for destino in (os.path.join(mod, "legendas", f"{nome_f}.{a.idioma}.srt"),
                        os.path.join(mod, "video", f"{nome_f}.{a.idioma.lower()}.srt")):
            with open(destino, "w", encoding="utf-8", newline="\n") as o:
                o.write(srt)
    with open(os.path.join(mod, "fontes", f"traducao.{a.idioma}.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("recurso\tlinha\testado_en\ten\ttexto\n")
        for row in lado:
            o.write("\t".join(str(x).replace("\n", "\\n") for x in row) + "\n")
    with open(os.path.join(mod, "mod.toml"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f'formato    = 1\nid         = "{idf}"\nnome       = "{nome}"\nversao     = "{a.versao}"\nprioridade = 10\n'
                f'idiomas    = ["{a.idioma}"]\n'
                "# americano e os tres PAL: as mesmas vozes e as falas dos filmes nos mesmos tempos (como o legendas-pt-br)\n"
                f'jogo       = [{", ".join(chr(34) + j + chr(34) for j in JOGOS)}]\nprovisorio = true\n')
    n_filmes = sum(len(v) for v in filmes.values())
    with open(os.path.join(mod, "README.md"), "w", encoding="utf-8", newline="\n") as o:
        o.write(f"""# {nome} ({a.versao})

Legendas em {nome_pt} para o Blue Stinger Recompiled: tradução por IA das legendas em inglês (pacote `legendas-en`), **ainda
não revisada por humanos**. Os tempos são os do inglês; uma linha longa vira mais de uma legenda seguida. Só texto: nenhum
áudio ou vídeo do jogo.

- `legendas/falas.{a.idioma}.toml`: as falas do gameplay ({len(falas)} legendas).
- `legendas/<FILME>.{a.idioma}.srt` e `video/<FILME>.{a.idioma.lower()}.srt`: os filmes ({n_filmes} legendas).
- `fontes/traducao.{a.idioma}.tsv`: inglês e tradução lado a lado.

**Uso:** mod padrão do projeto (`mods-padrao/`), instalado pelo Builder em toda instalação. No jogo: Esc, Legendas,
Idioma; pela linha de comando, `--legendas {a.idioma}`.
""")
    print(f"{idf}: {len(falas)} legendas de falas, {n_filmes} de filmes em {len(filmes)} filmes; {divididas} linhas divididas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
