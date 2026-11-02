#!/usr/bin/env python3
"""Identidade funcional de um executavel PE (N8.8, regressao incremental).

O build de release nao e reprodutivel byte a byte: mudam so metadados de build. Este script zera apenas os campos
comprovadamente nao funcionais, lidos pela estrutura do PE (nunca por busca solta):
  - TimeDateStamp do cabecalho COFF;
  - TimeDateStamp de cada entrada do diretorio de depuracao;
  - GUID e Age do registro CodeView RSDS (identidade do PDB);
  - a data de build (BS_BUILD_DATE, app/CMakeLists.txt: "AAAA-MM-DD HH:MM") e a versao (BS_VERSION: o
    `git describe --always --dirty --abbrev=7` ou o VERSION do kit), usadas so no titulo da janela, em duas impressoes
    e no comentario de cabecalho do BS_GRAVAR (app/main_sdl.cpp), nunca na logica: cada uma e normalizada so no executavel
    do JOGO (com a literal do titulo, JOGO_RE; num exe ou numa DLL sem ela, uma data qualquer pode ser dado funcional) e so
    se houver EXATAMENTE uma literal no formato, terminada em zero, nas secoes de dados somente leitura; com zero ou mais
    de uma, nada e normalizado (na duvida, a identidade muda). A versao continua fora da identidade de proposito: nenhuma
    prova compara esse texto (revisao Codex 2026-11-01).
Qualquer outra diferenca muda a identidade. Arquivos que nao sao PE entram crus (SHA-256 do conteudo).

  python tools/dev/exe_identidade.py <arquivo> [...]          imprime "<identidade> <arquivo>"
  python tools/dev/exe_identidade.py --campos <arquivo>       lista os intervalos normalizados
  python tools/dev/exe_identidade.py --comparar <a> <b>       diferencas cruas e se todas caem nos campos normalizados
  python tools/dev/exe_identidade.py --arvore <caminho> [...]   identidade de arquivos ou pastas inteiras
  python tools/dev/exe_identidade.py --lista <arq> [<detalhe>]  identidade conjunta de uma lista de caminhos (regressao.ps1)
"""
import hashlib
import re
import struct
import sys

DATA_RE = re.compile(rb"\x00(20\d\d-[01]\d-[0-3]\d [0-2]\d:[0-5]\d)\x00")
VERSAO_RE = re.compile(rb"\x00([0-9a-f]{7}(?:-dirty)?)\x00")
JOGO_RE = re.compile(rb"\x00Blue Stinger \(recompilado\)\x00")  # kTitle de app/main_sdl.cpp


class NaoPE(Exception):
    pass


def secoes(b, pe):
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    base = pe + 24 + opt
    out = []
    for i in range(nsec):
        o = base + 40 * i
        nome = b[o:o + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", b, o + 8)
        carac = struct.unpack_from("<I", b, o + 36)[0]
        out.append((nome, va, max(vsize, rsize), rptr, rsize, carac))
    return out


def rva_para_arquivo(secs, rva):
    for _, va, vs, rptr, rsize, _ in secs:
        if va <= rva < va + vs and rva - va < rsize:
            return rptr + (rva - va)
    return None


def campos(b):
    """Intervalos [inicio, fim) normalizados, com o motivo."""
    if len(b) < 0x40 or b[:2] != b"MZ":
        raise NaoPE("sem MZ")
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\x00\x00":
        raise NaoPE("sem assinatura PE")
    out = [(pe + 8, pe + 12, "COFF TimeDateStamp")]
    magic = struct.unpack_from("<H", b, pe + 24)[0]
    dd = pe + 24 + (112 if magic == 0x20B else 96)  # diretorios de dados (PE32+ / PE32)
    secs = secoes(b, pe)
    dbg_rva, dbg_size = struct.unpack_from("<II", b, dd + 6 * 8)
    if dbg_rva and dbg_size:
        off = rva_para_arquivo(secs, dbg_rva)
        if off is None:
            raise NaoPE("diretorio de depuracao fora das secoes")
        for k in range(dbg_size // 28):
            e = off + 28 * k
            out.append((e + 4, e + 8, "debug[%d] TimeDateStamp" % k))
            tipo, tam, _, ptr = struct.unpack_from("<IIII", b, e + 12)
            if tipo == 2 and ptr and b[ptr:ptr + 4] == b"RSDS":  # CodeView
                out.append((ptr + 4, ptr + 24, "RSDS GUID e Age"))
    # a data e a versao de build: so no jogo, nas secoes de dados somente leitura (inicializadas, sem escrita nem execucao)
    leitura = [s for s in secs if s[5] & 0x40 and not s[5] & 0x80000000 and not s[5] & 0x20000000]
    if not any(JOGO_RE.search(b, s[3], s[3] + s[4]) for s in leitura):
        return sorted(out)
    for regex, motivo in ((DATA_RE, "BS_BUILD_DATE"), (VERSAO_RE, "BS_VERSION")):
        achadas = []
        for _, _, _, rptr, rsize, _ in leitura:
            pos = rptr  # recomeca no zero final: literais vizinhas compartilham o separador
            while True:
                m = regex.search(b, pos, rptr + rsize)
                if not m:
                    break
                achadas.append((m.start(1), m.end(1)))
                pos = m.end(1)
        if len(achadas) == 1:
            out.append((achadas[0][0], achadas[0][1], motivo))
    return sorted(out)


def ler(caminho):
    with open(caminho, "rb") as f:
        return f.read()


def identidade(caminho):
    b = bytearray(ler(caminho))
    try:
        cs = campos(b)
    except NaoPE:
        return "raw-" + hashlib.sha256(b).hexdigest()[:32]
    for a, z, _ in cs:
        b[a:z] = b"\x00" * (z - a)
    return "pe-" + hashlib.sha256(b).hexdigest()[:32]


def comparar(a, b):
    x, y = ler(a), ler(b)
    if len(x) != len(y):
        print("tamanhos diferentes: %d x %d" % (len(x), len(y)))
        return 1
    dif = [i for i in range(len(x)) if x[i] != y[i]]
    cs = campos(bytearray(x)) + campos(bytearray(y))
    fora = [i for i in dif if not any(a0 <= i < z for a0, z, _ in cs)]
    print("bytes diferentes: %d; fora dos campos normalizados: %d%s" % (len(dif), len(fora),
          "" if not fora else " (" + " ".join("0x%X" % i for i in fora[:16]) + ")"))
    for a0, z, m in sorted(set(cs)):
        n = sum(1 for i in dif if a0 <= i < z)
        if n:
            print("  0x%X-0x%X %s: %d byte(s)" % (a0, z, m, n))
    print("identidades: %s %s" % (identidade(a), identidade(b)))
    return 0 if not fora else 2


def arquivo(caminho):
    """Identidade de um arquivo: .exe/.dll pelo PE normalizado; o resto cru."""
    if caminho.lower().endswith((".exe", ".dll")):
        return identidade(caminho)
    h = hashlib.sha256()
    with open(caminho, "rb") as f:
        for bloco in iter(lambda: f.read(1 << 20), b""):
            h.update(bloco)
    return "raw-" + h.hexdigest()[:32]


def arvore(raiz):
    """Identidade de um arquivo ou de uma pasta inteira (caminhos relativos e identidade de cada arquivo, em ordem).
    Ausente tambem e uma identidade (a etapa que dependia dela nao reaproveita uma aprovacao com ela presente)."""
    import os
    if os.path.isfile(raiz):
        return arquivo(raiz)
    if not os.path.isdir(raiz):
        return "ausente"
    linhas = []
    for d, subs, fs in os.walk(raiz):
        subs.sort()
        for n in sorted(fs):
            c = os.path.join(d, n)
            linhas.append("%s\t%s" % (os.path.relpath(c, raiz).replace("\\", "/").lower(), arquivo(c)))
    return "tree-%d-%s" % (len(linhas), hashlib.sha256("\n".join(linhas).encode("utf-8")).hexdigest()[:32])


def lista(arq_lista, detalhe=None):
    """Identidade conjunta de uma lista de caminhos (um por linha, arquivo ou pasta; a ordem e o texto da linha entram).
    Com `detalhe`, grava a identidade de cada linha (para achar o que mudou)."""
    linhas = []
    with open(arq_lista, encoding="utf-8-sig") as f:
        for l in f:
            c = l.rstrip("\r\n")
            if c:
                linhas.append("%s\t%s" % (c.lower(), arvore(c)))
    if detalhe:
        with open(detalhe, "w", encoding="utf-8") as f:
            f.write("\n".join(linhas) + "\n")
    return "lista-%d-%s" % (len(linhas), hashlib.sha256("\n".join(linhas).encode("utf-8")).hexdigest()[:32])


def main(argv):
    if len(argv) >= 3 and argv[1] == "--lista":  # --lista <arquivo> [<detalhe>]: uma identidade para tudo
        print(lista(argv[2], argv[3] if len(argv) > 3 else None))
        return 0
    if len(argv) >= 3 and argv[1] == "--arvore":  # uma linha por argumento: "<identidade>\t<caminho>"
        for c in argv[2:]:
            print("%s\t%s" % (arvore(c), c))
        return 0
    if len(argv) >= 3 and argv[1] == "--comparar":
        return comparar(argv[2], argv[3])
    if len(argv) >= 3 and argv[1] == "--campos":
        for a, z, m in campos(bytearray(ler(argv[2]))):
            print("0x%X-0x%X %s" % (a, z, m))
        return 0
    for c in argv[1:]:
        print(identidade(c), c)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
