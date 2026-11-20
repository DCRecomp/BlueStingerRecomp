"""PAL P1 (docs/PAL-PLANO.md): mapa de enderecos de uma versao do 1ST_READ.BIN para outra (so leitura).

Etapa 1, funcoes: cada funcao da versao base (functions.tsv da analise) e procurada na outra versao pelo corpo
NORMALIZADO: palavras de 32 bits alinhadas com valor em 8C000000-8CFFFFFF (literais de endereco) e os deslocamentos
de bra/bsr ficam mascarados; o resto tem de ser identico. Enumera todos os candidatos numa janela de deslocamentos
(-512..+4096); aceita o unico, e entre varios so o unico coerente (alvos de bra/bsr e literais de codigo traduzidos pelo
mapa ja feito, pelo menos um conferido, nenhum divergente). Depois as referencias que chegam a
entrada, e por ultimo a posicao entre vizinhas mapeadas de mesmo deslocamento. Os demais ficam em pendentes.tsv.
Etapa 2, verificacao: cada literal mascarado da base que aponta para codigo mapeado tem de valer, na outra versao,
o endereco traduzido; cada bra/bsr tambem. Divergencias sao listadas.

Uso: python -I versao_mapa.py <base.BIN> <outra.BIN> <functions.tsv> <saida_dir>
"""
import bisect
import collections
import csv
import os
import struct
import sys

BASE = 0x8C010000
# janela de deslocamentos procurada (bytes antes, depois); BS_MAPA_JANELA=antes,depois muda (outra compilacao, como a
# japonesa, desloca mais que a PAL)
JANELA = tuple(int(x, 0) for x in os.environ.get("BS_MAPA_JANELA", "512,4096").split(","))


def le_funcoes(caminho, tam):
    fs = []
    for row in csv.DictReader(open(caminho, encoding="utf-8"), delimiter="\t"):
        a = int(row["entrada"], 16)
        if BASE <= a < BASE + tam:
            fs.append(a)
    return sorted(set(fs))


def eh_end(v):
    return 0x8C000000 <= v < 0x8D000000


def normaliza(img, ini, fim, base_end):
    """Corpo [ini, fim) da imagem (enderecos absolutos); devolve bytes com literais e bra/bsr mascarados."""
    o0, o1 = ini - BASE, fim - BASE
    b = bytearray(img[o0:o1])
    # literais: palavras alinhadas a 4 (no endereco absoluto)
    p = (4 - (ini & 3)) & 3
    while p + 4 <= len(b):
        v = struct.unpack_from("<I", b, p)[0]
        if eh_end(v):
            b[p:p + 4] = b"\xEE\xEE\xEE\xEE"
        p += 4
    # bra (Axxx) e bsr (Bxxx): mascara o deslocamento de 12 bits (so as meias-palavras nao mascaradas)
    for q in range(0, len(b) - 1, 2):
        h = b[q] | (b[q + 1] << 8)
        if h == 0xEEEE:
            continue
        if (h >> 12) in (0xA, 0xB):
            b[q] = 0
            b[q + 1] = (h >> 8) & 0xF0
    return bytes(b)


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    base_img = open(sys.argv[1], "rb").read()
    out_img = open(sys.argv[2], "rb").read()
    funcs = le_funcoes(sys.argv[3], len(base_img))
    saida = sys.argv[4]
    os.makedirs(saida, exist_ok=True)
    fim_base = BASE + len(base_img)
    fim_out = BASE + len(out_img)

    limites = funcs + [fim_base]
    # a normalizacao depende so do endereco absoluto (alinhamento a 4), entao vale normalizar as imagens inteiras uma vez
    norm_base = normaliza(base_img, BASE, fim_base, BASE)
    norm_out = normaliza(out_img, BASE, fim_out, BASE)
    corpos = {a: norm_base[a - BASE:limites[i + 1] - BASE] for i, a in enumerate(funcs)}

    def candidatos(a):
        """Todos os deslocamentos d (pares, -512..+4096) em que o corpo normalizado de a aparece na outra imagem."""
        c = corpos[a]
        ini = max(0, a - BASE - JANELA[0])
        fim = min(len(norm_out), a - BASE + JANELA[1] + len(c))
        res = []
        p = norm_out.find(c, ini, fim)
        while p >= 0:
            if not (p & 1):
                res.append(p - (a - BASE))
            p = norm_out.find(c, p + 1, fim)
        return res

    indice = {a: i for i, a in enumerate(funcs)}
    mapa = {}
    estado = {"chaves": []}

    def traduz(v):
        """Endereco de codigo da base -> outra versao, pela funcao que o contem (None se nao mapeado)."""
        chaves = estado["chaves"]
        i = bisect.bisect_right(chaves, v) - 1
        if i < 0:
            return None
        a = chaves[i]
        if v >= limites[indice[a] + 1]:
            return None
        return v + mapa[a]

    def coerente(a, d):
        """Confere os alvos de bra/bsr e os literais de codigo de a no deslocamento d pelo mapa atual.
        Devolve (conferidos, divergentes)."""
        ini, fim = a, limites[indice[a] + 1]
        ok = ruim = 0
        mascarados = set()
        p = ini + ((4 - (ini & 3)) & 3)
        while p + 4 <= fim:
            v = struct.unpack_from("<I", base_img, p - BASE)[0]
            if eh_end(v):
                mascarados.update((p, p + 2))
                t = traduz(v) if BASE <= v < fim_base else None
                if t is not None:
                    w = struct.unpack_from("<I", out_img, p + d - BASE)[0]
                    ok, ruim = (ok + 1, ruim) if t == w else (ok, ruim + 1)
            p += 4
        for q in range(ini, fim - 1, 2):
            if q in mascarados:
                continue
            h = struct.unpack_from("<H", base_img, q - BASE)[0]
            if (h >> 12) in (0xA, 0xB):
                disp = h & 0xFFF
                disp = disp - 0x1000 if disp & 0x800 else disp
                t = traduz(q + 4 + disp * 2)
                if t is None:
                    continue
                h2 = struct.unpack_from("<H", out_img, q + d - BASE)[0]
                disp2 = h2 & 0xFFF
                disp2 = disp2 - 0x1000 if disp2 & 0x800 else disp2
                alvo2 = q + d + 4 + disp2 * 2
                ok, ruim = (ok + 1, ruim) if t == alvo2 else (ok, ruim + 1)
        return ok, ruim

    # passada 1: so os casamentos unicos na janela
    cands = {}
    for a in funcs:
        c = candidatos(a)
        if len(c) == 1:
            mapa[a] = c[0]
        elif c:
            cands[a] = c
    sem = [a for a in funcs if a not in mapa and a not in cands]
    # passadas seguintes: as ambiguas ficam com o unico candidato coerente (alvos e literais conferidos, >= 1, nenhum
    # divergente) pelo mapa atual; repete enquanto resolver alguma
    resolvidas_por_alvo = 0
    while True:
        estado["chaves"] = sorted(mapa)
        novas = {}
        for a, c in cands.items():
            bons = []
            for d in c:
                ok, ruim = coerente(a, d)
                if ok >= 1 and ruim == 0:
                    bons.append(d)
            if len(bons) == 1:
                novas[a] = bons[0]
        # referencias que CHEGAM: literais e bra/bsr das funcoes mapeadas que apontam para a entrada de uma ambigua dao
        # o endereco dela na outra versao; vale se todas concordam e o deslocamento e um dos candidatos
        chegam = {}
        for b in estado["chaves"]:
            db, ini, fim = mapa[b], b, limites[indice[b] + 1]
            masc = set()
            p = ini + ((4 - (ini & 3)) & 3)
            while p + 4 <= fim:
                v = struct.unpack_from("<I", base_img, p - BASE)[0]
                if eh_end(v):
                    masc.update((p, p + 2))
                    if v in cands:
                        chegam.setdefault(v, set()).add(struct.unpack_from("<I", out_img, p + db - BASE)[0])
                p += 4
            for q in range(ini, fim - 1, 2):
                if q in masc:
                    continue
                h = struct.unpack_from("<H", base_img, q - BASE)[0]
                if (h >> 12) in (0xA, 0xB):
                    disp = h & 0xFFF
                    disp = disp - 0x1000 if disp & 0x800 else disp
                    t = q + 4 + disp * 2
                    if t in cands:
                        h2 = struct.unpack_from("<H", out_img, q + db - BASE)[0]
                        disp2 = h2 & 0xFFF
                        disp2 = disp2 - 0x1000 if disp2 & 0x800 else disp2
                        chegam.setdefault(t, set()).add(q + db + 4 + disp2 * 2)
        for a, ws in chegam.items():
            if a in novas or len(ws) != 1:
                continue
            d = next(iter(ws)) - a
            if d in cands[a]:
                novas[a] = d
        if not novas:
            break
        for a, d in novas.items():
            mapa[a] = d
            del cands[a]
        resolvidas_por_alvo += len(novas)
    # por ultimo, a posicao: uma sequencia de ambiguas entre duas vizinhas mapeadas com o MESMO deslocamento d, em que
    # todas tem d entre os candidatos, ocupa na outra versao exatamente o trecho entre as vizinhas (mesmo tamanho)
    resolvidas_por_posicao = 0
    i = 0
    while i < len(funcs):
        if funcs[i] not in cands:
            i += 1
            continue
        j = i
        while j < len(funcs) and funcs[j] in cands:
            j += 1
        ant = funcs[i - 1] if i > 0 else None
        seg = funcs[j] if j < len(funcs) else None
        if ant in mapa and seg in mapa and mapa[ant] == mapa[seg]:
            d = mapa[ant]
            if all(d in cands[funcs[k]] for k in range(i, j)):
                for k in range(i, j):
                    mapa[funcs[k]] = d
                    del cands[funcs[k]]
                    resolvidas_por_posicao += 1
        i = j
    ambiguas = sorted((a, c) for a, c in cands.items())
    estado["chaves"] = sorted(mapa)

    # monotonia: o endereco traduzido tem de crescer com o da base
    ordem = [(a, a + mapa[a]) for a in funcs if a in mapa]
    fora_ordem = [ordem[i][0] for i in range(1, len(ordem)) if ordem[i][1] <= ordem[i - 1][1]]

    # verificacao dos literais e bra/bsr
    lit_ok = lit_ruim = lit_dados = br_ok = br_ruim = 0
    divergencias = []
    for i, a in enumerate(funcs):
        if a not in mapa:
            continue
        d = mapa[a]
        ini, fim = a, limites[i + 1]
        p = ini + ((4 - (ini & 3)) & 3)
        mascarados = set()
        while p + 4 <= fim:
            v = struct.unpack_from("<I", base_img, p - BASE)[0]
            if eh_end(v):
                mascarados.update((p, p + 2))
                w = struct.unpack_from("<I", out_img, p + d - BASE)[0]
                if BASE <= v < fim_base:
                    t = traduz(v)
                    if t is None:
                        lit_dados += 1
                    elif t == w:
                        lit_ok += 1
                    else:
                        lit_ruim += 1
                        divergencias.append(("literal", a, p, v, w, t))
                p += 4
            else:
                p += 4
        for q in range(ini, fim - 1, 2):
            if q in mascarados:
                continue
            h = struct.unpack_from("<H", base_img, q - BASE)[0]
            if (h >> 12) in (0xA, 0xB):
                disp = h & 0xFFF
                if disp & 0x800:
                    disp -= 0x1000
                alvo = q + 4 + disp * 2
                t = traduz(alvo)
                h2 = struct.unpack_from("<H", out_img, q + d - BASE)[0]
                disp2 = h2 & 0xFFF
                if disp2 & 0x800:
                    disp2 -= 0x1000
                alvo2 = q + d + 4 + disp2 * 2
                if t is None:
                    continue
                if t == alvo2:
                    br_ok += 1
                else:
                    br_ruim += 1
                    divergencias.append(("bra/bsr", a, q, alvo, alvo2, t))

    # etapa 3, dados: os literais das funcoes mapeadas que apontam para fora do codigo mapeado (dados do executavel,
    # BSS, RAM) dao pares base -> outra. Um mesmo valor da base tem de dar sempre o mesmo valor na outra.
    pares = {}
    for i, a in enumerate(funcs):
        if a not in mapa:
            continue
        d = mapa[a]
        p = a + ((4 - (a & 3)) & 3)
        while p + 4 <= limites[i + 1]:
            v = struct.unpack_from("<I", base_img, p - BASE)[0]
            if eh_end(v) and traduz(v) is None:
                w = struct.unpack_from("<I", out_img, p + d - BASE)[0]
                pares.setdefault(v, collections.Counter())[w] += 1
            p += 4
    conflitos = {v: c for v, c in pares.items() if len(c) > 1}
    with open(os.path.join(saida, "dados.tsv"), "w", encoding="utf-8") as f:
        f.write("base\toutra\tdelta\tusos\tconflito\n")
        for v in sorted(pares):
            w, n = pares[v].most_common(1)[0]
            f.write("%08X\t%08X\t%+d\t%d\t%s\n" % (v, w, w - v, sum(pares[v].values()),
                                                   ",".join("%08X" % x for x in pares[v]) if v in conflitos else "-"))
    dfaixas, atual = [], None
    for v in sorted(pares):
        if v in conflitos:
            continue
        w = pares[v].most_common(1)[0][0]
        if atual and atual[2] == w - v:
            atual[1] = v
            atual[3] += 1
        else:
            atual = [v, v, w - v, 1]
            dfaixas.append(atual)
    with open(os.path.join(saida, "dados_faixas.tsv"), "w", encoding="utf-8") as f:
        f.write("de\tate\tdelta\tenderecos\n")
        for x in dfaixas:
            f.write("%08X\t%08X\t%+d\t%d\n" % tuple(x))

    with open(os.path.join(saida, "funcoes.tsv"), "w", encoding="utf-8") as f:
        f.write("base\toutra\tdelta\ttamanho\n")
        for i, a in enumerate(funcs):
            n = limites[i + 1] - a
            if a in mapa:
                f.write("%08X\t%08X\t%+d\t%d\n" % (a, a + mapa[a], mapa[a], n))
            else:
                f.write("%08X\t-\t-\t%d\n" % (a, n))
    with open(os.path.join(saida, "pendentes.tsv"), "w", encoding="utf-8") as f:
        f.write("tipo\tbase\ttamanho\tcandidatos\n")
        for a, c in ambiguas:
            f.write("ambigua\t%08X\t%d\t%s\n" % (a, len(corpos[a]), ",".join("%+d" % d for d in c)))
        for a in sem:
            f.write("sem_par\t%08X\t%d\t-\n" % (a, len(corpos[a])))
    with open(os.path.join(saida, "divergencias.tsv"), "w", encoding="utf-8") as f:
        f.write("tipo\tfuncao\tonde\tvalor_base\tvalor_outra\tesperado\n")
        for t, a, p, v, w, e in divergencias:
            f.write("%s\t%08X\t%08X\t%08X\t%08X\t%08X\n" % (t, a, p, v, w, e))

    faixas, atual = [], None
    for a in funcs:
        if a not in mapa:
            continue
        if atual and atual[2] == mapa[a]:
            atual[1] = a
            atual[3] += 1
        else:
            atual = [a, a, mapa[a], 1]
            faixas.append(atual)
    with open(os.path.join(saida, "faixas.tsv"), "w", encoding="utf-8") as f:
        f.write("de\tate\tdelta\tfuncoes\n")
        for x in faixas:
            f.write("%08X\t%08X\t%+d\t%d\n" % tuple(x))

    bytes_base = sum(limites[i + 1] - a for i, a in enumerate(funcs))
    bytes_map = sum(limites[i + 1] - a for i, a in enumerate(funcs) if a in mapa)
    print("funcoes da base: %d (%d B)" % (len(funcs), bytes_base))
    print("mapeadas: %d (%.1f %% dos bytes); ambiguas %d; sem par %d; fora de ordem %d" % (
        len(mapa), 100.0 * bytes_map / bytes_base, len(ambiguas), len(sem), len(fora_ordem)))
    print("ambiguas resolvidas pelos alvos e pelas referencias que chegam: %d" % resolvidas_por_alvo)
    print("ambiguas resolvidas pela posicao entre vizinhas do mesmo deslocamento: %d" % resolvidas_por_posicao)
    print("faixas de deslocamento: %d" % len(faixas))
    print("literais de codigo: %d coerentes, %d divergentes, %d para codigo nao mapeado" % (lit_ok, lit_ruim, lit_dados))
    print("bra/bsr: %d coerentes, %d divergentes" % (br_ok, br_ruim))
    print("dados: %d enderecos distintos, %d com conflito, %d faixas de deslocamento" % (
        len(pares), len(conflitos), len(dfaixas)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
