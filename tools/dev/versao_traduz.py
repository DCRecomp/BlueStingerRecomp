"""PAL P1 (docs/PAL-PLANO.md), etapa 4: traduz os enderecos fixos fora do C++ gerado (configs e codigo nativo) pelo
mapa do versao_mapa.py. So leitura.

Classes:
  sistema      < 8C010000 (area do sistema / IP.BIN): igual nas duas versoes, por hipotese a conferir
  funcao       entrada de funcao mapeada
  interior     dentro de funcao mapeada (entrada + deslocamento)
  dado         endereco citado por literal de funcao mapeada (par exato do mapa)
  faixa        entre dois pares com o mesmo deslocamento, a no maximo FAIXA_MAX bytes (interpolado)
  pendente     codigo em funcao sem par, ou dado sem faixa coerente: revisao a mao
  ram_alta     acima do ultimo par de dados (pilha/heap): revisao a mao
  nao_endereco literal que a varredura pegou mas nao e endereco (cor, constante): "ignorar" no excecoes.tsv
  excecao      decidido a mao no arquivo de excecoes (<mapa_dir>/excecoes.tsv: base, outra, evidencia); vale sobre
               as classes acima, e o conteudo e conferido do mesmo jeito

Uso: python -I versao_traduz.py <repo> <mapa_dir> <base.BIN> <outra.BIN> [saida.tsv] [--inc <tabela.inc>]

--inc grava a tabela de pares para runtime/versao/enderecos.h (recusa se houver pendente ou conteudo divergente).

Conferencia: em funcao/interior, a meia-palavra (instrucao) normalizada tem de ser igual nos dois; em dado/faixa dentro
do executavel, os 4 bytes normalizados (literal de endereco mascarado). BSS/RAM fora do executavel: sem conteudo a
conferir ("-").
"""
import bisect
import hashlib
import os
import re
import sys

BASE = 0x8C010000
RX = re.compile(r"0x8[cC][0-9a-fA-F]{6}")
RX_RAM_LINHA = re.compile(r"\b(ram\w*|g_ram\w*)\s*(\+|\[)", re.I)
RX_OFS = re.compile(r"(?<![0-9A-Za-z_])(0x[0-9a-fA-F]{5,6})u?\b")
PASTAS = ("config", "runtime", "app", "transitional", "native", "builder")
FORA = ("generated", "third_party", "_deps", "tests", "build", "dist", "tmp", "versao")  # versao: as proprias tabelas
EXT = (".cpp", ".h", ".inc", ".toml", ".hpp")
OFS_BSS = (0x1DA220, 0x32BBC0)  # BSS/RAM do jogo como deslocamento (do mapa PAL DE; conferir em outras versoes)
FAIXA_MAX = 4096  # distancia maxima entre as duas ancoras de uma interpolacao (revisao Codex 2026-11-19)


def le_tsv(caminho):
    linhas = open(caminho, encoding="utf-8").read().splitlines()
    cab = linhas[0].split("\t")
    return [dict(zip(cab, x.split("\t"))) for x in linhas[1:] if x]


def main():
    args = sys.argv[1:]
    inc = None
    if "--inc" in args:
        k = args.index("--inc")
        inc = args[k + 1]
        del args[k:k + 2]
    if len(args) < 4:
        print(__doc__)
        return 2
    repo, mapa_dir = args[0], args[1]
    base_img = open(args[2], "rb").read()
    out_img = open(args[3], "rb").read()
    saida = args[4] if len(args) > 4 else os.path.join(mapa_dir, "enderecos.tsv")

    def palavra(img, a, n):
        o = a - BASE
        if a < BASE or o + n > len(img):
            return None
        return img[o:o + n]

    def norm4(b):
        v = int.from_bytes(b, "little")
        return b"ADDR" if 0x8C000000 <= v < 0x8D000000 else b

    def norm2(b):
        h = int.from_bytes(b, "little")
        return bytes([0, (h >> 8) & 0xF0]) if (h >> 12) in (0xA, 0xB) else b

    def literal_pc(img, a):
        """mov.w/mov.l @(disp,PC),Rn (9ndd/Dndd): (opcode, registrador, valor carregado) ou None. Entre compilacoes (o
        japones) o pool de literais muda de lugar: o deslocamento difere, o que a instrucao carrega nao (JP P-J1)."""
        h = palavra(img, a & ~1, 2)
        if h is None:
            return None
        x = int.from_bytes(h, "little")
        if x >> 12 == 0x9:
            q = palavra(img, (a & ~1) + 4 + (x & 0xFF) * 2, 2)
            return (9, (x >> 8) & 0xF, q) if q else None
        if x >> 12 == 0xD:
            q = palavra(img, ((a & ~1) & ~3) + 4 + (x & 0xFF) * 4, 4)
            return (0xD, (x >> 8) & 0xF, norm4(q)) if q else None
        return None

    def confere(c, v, w):
        if w is None:
            return "-"
        if c == "excecao" and v >= BASE and w >= BASE:
            # excecao dentro do codigo: a instrucao (bra/bsr mascarados; literal pelo valor carregado) ou os 4 bytes
            a, b = palavra(base_img, v & ~1, 2), palavra(out_img, w & ~1, 2)
            if a and b and (norm2(a) == norm2(b) or (literal_pc(base_img, v) and literal_pc(base_img, v) == literal_pc(out_img, w))):
                return "igual"
        if c in ("funcao", "interior"):
            a, b = palavra(base_img, v & ~1, 2), palavra(out_img, w & ~1, 2)
            if a is None or b is None:
                return "-"
            # pode ser o meio de um literal: aceita tambem os 4 bytes alinhados normalizados
            if norm2(a) == norm2(b):
                return "igual"
            a4, b4 = palavra(base_img, v & ~3, 4), palavra(out_img, w & ~3, 4)
            return "igual" if a4 and b4 and norm4(a4) == norm4(b4) else "DIFERENTE"
        if c in ("dado", "faixa", "excecao"):
            a, b = palavra(base_img, v & ~3, 4), palavra(out_img, w & ~3, 4)
            if a is None or b is None:
                return "-"
            return "igual" if norm4(a) == norm4(b) else "DIFERENTE"
        return "-"

    funcs = []  # (base, fim, delta|None)
    for r in le_tsv(os.path.join(mapa_dir, "funcoes.tsv")):
        a = int(r["base"], 16)
        funcs.append((a, a + int(r["tamanho"]), None if r["delta"] == "-" else int(r["delta"])))
    entradas = [f[0] for f in funcs]
    dados = {}
    for r in le_tsv(os.path.join(mapa_dir, "dados.tsv")):
        if r["conflito"] == "-":
            dados[int(r["base"], 16)] = int(r["outra"], 16)
    dchaves = sorted(dados)
    # o fim do codigo "real" (a funcao 8C09BA04 da tabela engole a regiao de dados ate 8C157F60): trata como dados tudo o
    # que tiver par de dados ou estiver entre pares de dados
    ram_topo = dchaves[-1]

    excecoes = {}
    cam_exc = os.path.join(mapa_dir, "excecoes.tsv")
    if os.path.exists(cam_exc):
        for r in le_tsv(cam_exc):
            o = r["outra"]
            excecoes[int(r["base"], 16)] = ("ignorar" if o == "ignorar" else None if o == "-" else int(o, 16),
                                            r["evidencia"])

    def classifica(v):
        if v in excecoes:
            w = excecoes[v][0]
            return ("nao_endereco", None) if w == "ignorar" else ("excecao", w)
        if v < BASE:
            return "sistema", v
        if v in dados:
            return "dado", dados[v]
        i = bisect.bisect_right(entradas, v) - 1
        if i >= 0 and v < funcs[i][1]:
            a, fim, d = funcs[i]
            # se a funcao e a "engolidora" da regiao de dados, decide pelos pares de dados
            if fim - a < 65536:
                if d is None:
                    return "pendente", None
                return ("funcao" if v == a else "interior"), v + d
        if v > ram_topo:
            return "ram_alta", None
        j = bisect.bisect_right(dchaves, v)
        if 0 < j < len(dchaves):
            lo, hi = dchaves[j - 1], dchaves[j]
            if dados[lo] - lo == dados[hi] - hi and hi - lo <= FAIXA_MAX:
                return "faixa", v + dados[lo] - lo
        return "pendente", None

    achados = {}
    for pasta in PASTAS:
        for raiz, subs, arqs in os.walk(os.path.join(repo, pasta)):
            subs[:] = [s for s in subs if s not in FORA]
            for nome in arqs:
                if not nome.endswith(EXT):
                    continue
                if nome.endswith(".toml") and "_us" not in nome:
                    continue
                cam = os.path.join(raiz, nome)
                rel = os.path.relpath(cam, repo).replace("\\", "/")
                for n, linha in enumerate(open(cam, encoding="utf-8", errors="replace"), 1):
                    for m in RX.finditer(linha):
                        v = int(m.group(0), 16)
                        achados.setdefault(v, []).append("%s:%d" % (rel, n))
                    # forma de deslocamento na RAM (ram_ + 0x24CCB8, ram_[0x240000], laco "a = 0x240000; a < 0x260000"
                    # numa linha que indexa a RAM): vira 8C000000 + deslocamento
                    na_ram = RX_RAM_LINHA.search(linha)
                    for m in RX_OFS.finditer(linha):
                        v = int(m.group(1), 16)
                        if (v + 1) & v == 0:
                            continue  # mascara (0xFFFFFF, 0x1FFFFF): nao e endereco
                        if pasta == "builder" and not na_ram:
                            continue  # o Builder nao le a RAM do jogo; la os literais desse tamanho sao cores
                        if na_ram and 0x10000 <= v < 0x1000000:
                            achados.setdefault(0x8C000000 + v, []).append("%s:%d(ram+)" % (rel, n))
                        elif not na_ram and OFS_BSS[0] <= v < OFS_BSS[1] and v % 0x1000:
                            # sem "ram" na linha: so deslocamentos na faixa da BSS que nao sejam numeros redondos
                            achados.setdefault(0x8C000000 + v, []).append("%s:%d(ofs?)" % (rel, n))

    contagem, conteudo = {}, {}
    pares = []
    with open(saida, "w", encoding="utf-8") as f:
        f.write("base\tclasse\toutra\tdelta\tconteudo\tusos\tonde\n")
        for v in sorted(achados):
            c, w = classifica(v)
            contagem[c] = contagem.get(c, 0) + 1
            k = confere(c, v, w)
            conteudo[k] = conteudo.get(k, 0) + 1
            pares.append((v, c, w, k))
            f.write("%08X\t%s\t%s\t%s\t%s\t%d\t%s\n" % (
                v, c, "%08X" % w if w is not None else "-", "%+d" % (w - v) if w is not None else "-", k,
                len(achados[v]), " ".join(achados[v][:4]) + (" ..." if len(achados[v]) > 4 else "")))
    print("enderecos distintos: %d" % len(achados))
    for c in ("sistema", "funcao", "interior", "dado", "faixa", "excecao", "nao_endereco", "pendente", "ram_alta"):
        print("  %-9s %d" % (c, contagem.get(c, 0)))
    print("conteudo: %s" % conteudo)
    if inc:
        ruins = [(v, c, k) for v, c, w, k in pares if c in ("pendente", "ram_alta") or k == "DIFERENTE"]
        if ruins:
            print("ERRO: --inc recusado; pendentes ou divergentes: %s" % ", ".join("%08X %s %s" % r for r in ruins[:10]))
            return 1
        sha = lambda b: hashlib.sha256(b).hexdigest()
        with open(inc, "w", encoding="utf-8", newline=chr(10)) as f:
            nl = chr(10)
            f.write("// Gerado por tools/dev/versao_traduz.py --inc; NAO EDITAR A MAO (docs/PAL-PLANO.md, secao 6)." + nl)
            f.write("// Pares (endereco americano, endereco desta versao) de todo endereco do jogo citado fora do C++"
                    " gerado." + nl)
            f.write("// base  1ST_READ.BIN sha256 %s" % sha(base_img) + nl)
            f.write("// outra 1ST_READ.BIN sha256 %s" % sha(out_img) + nl)
            for v, c, w, k in pares:
                if c == "nao_endereco":
                    continue
                f.write("BS_PAR(0x%08Xu, 0x%08Xu)  // %s%s" % (v, w if w is not None else v, c,
                                                               "" if k == "-" else ", conteudo " + k) + nl)
        print("tabela: %s (%d pares)" % (inc, sum(1 for x in pares if x[1] != "nao_endereco")))
    print("saida: %s" % saida)
    return 0


if __name__ == "__main__":
    sys.exit(main())
