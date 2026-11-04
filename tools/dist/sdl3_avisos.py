"""Gera os avisos de terceiros do SDL3 (docs/N9-L-LICENCAS.md, secao 7) a partir das fontes de uma versao fixa.

    python tools/dist/sdl3_avisos.py <fontes do SDL3> <saida.txt>          gera
    python tools/dist/sdl3_avisos.py <fontes do SDL3> <saida.txt> --conferir  confere se o arquivo commitado e o gerado

O SDL3 e zlib (LICENSE.txt), mas traz codigo de terceiros com avisos proprios (fdlibm da Sun em src/libm, yuv2rgb, HIDAPI,
cabecalhos Khronos, codigo da Valve, DirectX, X11...). A licenca de cada um pede que o aviso acompanhe a distribuicao
binaria. Este script junta, de cada arquivo de src/ e include/ com "Copyright" e sem o aviso do proprio SDL ("Sam
Lantinga"), o primeiro bloco de comentario com o aviso; blocos iguais aparecem uma vez, com a lista dos arquivos. Entram
todos os alvos (um conjunto maior que o de Windows e Linux nao faz mal); so os testes ficam fora.
"""
import os
import re
import sys

PULAR = {"test", "testgui", "hidtest", ".git", ".github"}
EXT = {".c", ".h", ".m", ".cpp", ".hpp", ".txt", ".in"}


def bloco(texto):
    # comentario /* ... */ com o aviso
    for m in re.finditer(r"/\*.*?\*/", texto, re.S):
        if re.search(r"copyright", m.group(0), re.I):
            linhas = [re.sub(r"^\s*(/\*+|\*+/|\*)?\s?", "", l).rstrip() for l in m.group(0).splitlines()]
            linhas = [l.replace("*/", "").rstrip() for l in linhas]
            while linhas and not linhas[0].strip("=*- "):
                linhas.pop(0)
            while linhas and not linhas[-1].strip("=*- "):
                linhas.pop()
            return "\n".join(linhas)
    # comentarios de linha (// ou #) seguidos com o aviso
    corrida = []
    for l in texto.splitlines():
        m = re.match(r"^\s*(//|#)\s?(.*)$", l)
        if m:
            corrida.append(m.group(2).rstrip())
            continue
        if any(re.search(r"copyright", c, re.I) for c in corrida):
            return "\n".join(corrida).strip("\n")
        corrida = []
    if any(re.search(r"copyright", c, re.I) for c in corrida):
        return "\n".join(corrida).strip("\n")
    return None


def gerar(raiz):
    blocos = {}
    for sub in ("src", "include"):
        for d, dirs, arqs in os.walk(os.path.join(raiz, sub)):
            dirs[:] = sorted(x for x in dirs if x not in PULAR)
            for a in sorted(arqs):
                if os.path.splitext(a)[1].lower() not in EXT:
                    continue
                p = os.path.join(d, a)
                t = open(p, encoding="utf-8", errors="replace").read()
                if not re.search(r"copyright", t, re.I) or "Sam Lantinga" in t:
                    continue
                b = bloco(t)
                if b:
                    blocos.setdefault(b, []).append(os.path.relpath(p, raiz).replace("\\", "/"))
    versao = "desconhecida"
    for c in ("VERSION.txt",):
        if os.path.isfile(os.path.join(raiz, c)):
            versao = open(os.path.join(raiz, c)).read().split()[0]
    cmake = os.path.join(raiz, "CMakeLists.txt")
    if os.path.isfile(cmake):
        m = re.search(r"project\(SDL3[^)]*VERSION\s+([0-9.]+)", open(cmake, encoding="utf-8").read())
        if m:
            versao = m.group(1)
    saida = ["Avisos de terceiros dentro do SDL3 %s (alem do LICENSE.txt do SDL, zlib)" % versao,
             "Gerado por tools/dist/sdl3_avisos.py a partir das fontes de src/ e include/ (sem os testes).",
             "Cada bloco e o aviso de copyright e licenca como aparece nas fontes; abaixo dele, os arquivos que o trazem.",
             "Textos completos referidos por estes avisos: HIDAPI-LICENSE*.txt e yuv2rgb-BSD-3.txt nesta pasta.", ""]
    for b, arqs in sorted(blocos.items(), key=lambda kv: kv[1][0]):
        saida.append("=" * 78)
        saida.append("Arquivos: " + ", ".join(arqs))
        saida.append("-" * 78)
        saida.append(b)
        saida.append("")
    return "\n".join(saida) + "\n", len(blocos), sum(len(v) for v in blocos.values())


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    texto, nb, na = gerar(sys.argv[1])
    if "--conferir" in sys.argv[3:]:
        atual = open(sys.argv[2], encoding="utf-8").read() if os.path.isfile(sys.argv[2]) else ""
        if atual != texto:
            print("DIFERE: %s nao e o gerado das fontes (%d blocos, %d arquivos)" % (sys.argv[2], nb, na))
            return 1
        print("ok: %d blocos, %d arquivos" % (nb, na))
        return 0
    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)
    print("%s: %d blocos, %d arquivos" % (sys.argv[2], nb, na))
    return 0


if __name__ == "__main__":
    sys.exit(main())
