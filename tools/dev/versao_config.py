"""PAL P3 (docs/PAL-PLANO.md): gera a configuracao do bs-recomp de outra versao a partir da americana.

Cada endereco do jogo (>= 8C010000) fora dos comentarios e traduzido pela tabela runtime/versao/include/versao/
enderecos_<versao>.inc (a mesma do bs::jogo; gerada pelo versao_traduz.py --inc a partir do mapa conferido). Um endereco
sem par e erro. Os comentarios continuam com os enderecos americanos (como no codigo nativo). [game] e [input] recebem os
metadados da versao.

Uso: python -I versao_config.py <config_us.toml> <tabela.inc> <saida.toml> --meta chave=valor ...
  ex.: --meta product_id=T13001D --meta ip_date=19990803 --meta region=E --meta first_read_size=1979556
       --meta first_read_sha256=<hash> --meta titulo="Blue Stinger PAL (alemao)"
"""
import re
import sys

RX = re.compile(r"(?<![\w.])0x8[cC][0-9a-fA-F]{6}\b")
RX_PAR = re.compile(r"BS_PAR\(0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\)")


def main():
    args = sys.argv[1:]
    meta = {}
    while "--meta" in args:
        k = args.index("--meta")
        chave, _, valor = args[k + 1].partition("=")
        meta[chave] = valor
        del args[k:k + 2]
    if len(args) != 3:
        print(__doc__)
        return 2
    us_toml, tabela, saida = args
    pares = {int(a, 16): int(b, 16) for a, b in RX_PAR.findall(open(tabela, encoding="utf-8").read())}
    linhas = open(us_toml, encoding="utf-8").read().split("\n")
    faltam, trocas = [], 0
    out = []
    secao = ""
    for n, linha in enumerate(linhas, 1):
        cod, sep, com = linha.partition("#")
        if re.match(r"\s*\[", cod):
            secao = cod.strip()

        def sub(m):
            nonlocal trocas
            v = int(m.group(0), 16)
            if v < 0x8C010000:
                return m.group(0)  # area do sistema: igual nas versoes
            if v not in pares:
                faltam.append((n, m.group(0)))
                return m.group(0)
            trocas += 1
            return "0x%08X" % pares[v]

        cod = RX.sub(sub, cod)
        # metadados da versao
        m = re.match(r'(\s*)(product_id|version|ip_date|region|first_read_sha256)(\s*=\s*)"[^"]*"(.*)$', cod)
        if m and m.group(2) in meta:
            cod = '%s%s%s"%s"%s' % (m.group(1), m.group(2), m.group(3), meta[m.group(2)], m.group(4))
            if m.group(2) == "first_read_sha256":
                sep, com = "#", " 1ST_READ.BIN desta versao"
        m = re.match(r"(\s*)(first_read_size)(\s*=\s*)\d+(.*)$", cod)
        if m and m.group(2) in meta:
            cod = "%s%s%s%s%s" % (m.group(1), m.group(2), m.group(3), meta[m.group(2)], m.group(4))
        out.append(cod + sep + com)
    if faltam:
        print("ERRO: enderecos sem traducao na tabela: %s" % ", ".join("linha %d %s" % f for f in faltam[:20]))
        return 1
    titulo = meta.get("titulo", "outra versao")
    cab = ["# GERADO por tools/dev/versao_config.py a partir de %s; NAO EDITAR A MAO (docs/PAL-PLANO.md, P3)." % us_toml.replace("\\", "/"),
           "# %s: os enderecos do jogo foram traduzidos pela tabela %s; os COMENTARIOS abaixo continuam com os"
           % (titulo, tabela.replace("\\", "/").split("/runtime/")[-1] if "/runtime/" in tabela.replace("\\", "/") else tabela),
           "# enderecos da versao americana (como o codigo nativo, bs::jogo).",
           ""]
    open(saida, "w", encoding="utf-8", newline="\n").write("\n".join(cab + out))
    print("%s: %d enderecos traduzidos" % (saida, trocas))
    return 0


if __name__ == "__main__":
    sys.exit(main())
