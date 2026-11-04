#!/usr/bin/env bash
# N9-L: primeiro passo no Ubuntu. Confere o disco de troca, cria e monta o ext4 de trabalho (um arquivo, sem mexer em
# particao nenhuma) e abre a pasta de evidencias.
#   bash /media/$USER/BS-TROCA/publico/ferramentas/ubuntu/preparar_sessao.sh [--tamanho 20G]
# Sem sudo: o arquivo e formatado como usuario comum e montado pelo udisks (o mesmo que monta pendrives na area de
# trabalho). So se o udisks recusar, oferece o mount com sudo, pedindo autorizacao.
set -uo pipefail
source "$(dirname "$0")/comum.sh"
tamanho=20G
[ "${1:-}" = "--tamanho" ] && tamanho=${2:?tamanho}

echo "== disco de troca: $BS_TROCA"
[ -d "$BS_PUBLICO" ] && [ -f "$BS_TROCA/SHA256SUMS-TROCA.txt" ] || falha "isto nao parece o disco de troca (falta publico/ ou SHA256SUMS-TROCA.txt)"
mkdir -p "$BS_EVID" || falha "nao consigo gravar em $BS_EVID (o disco esta somente leitura?)"

echo "== conferindo os arquivos copiados do Windows (alguns minutos: inclui o CDI)"
( cd "$BS_TROCA" && sha256sum --quiet --strict -c SHA256SUMS-TROCA.txt ) || falha "arquivo diferente do que saiu do Windows: copie de novo"
echo "   ok"
cp -f "$BS_PUBLICO/IDENTIFICACAO.txt" "$BS_EVID/IDENTIFICACAO.txt"

if [ -n "$(findmnt -n -o TARGET -S LABEL=BS-TRABALHO 2>/dev/null)" ]; then
    echo "== ext4 de trabalho ja montado em $(findmnt -n -o TARGET -S LABEL=BS-TRABALHO | head -1)"
else
    if [ ! -f "$BS_IMG" ]; then
        livre=$(df -B1G --output=avail "$BS_TROCA" | tail -1 | tr -d ' ')
        echo "== criar $BS_IMG ($tamanho; livres no disco de troca: ${livre} GB)"
        echo "   E um arquivo comum no disco de troca: nenhuma particao e criada ou alterada. Apague-o no Windows quando quiser."
        pergunta "Criar o arquivo de trabalho agora?" || falha "cancelado"
        truncate -s "$tamanho" "$BS_IMG" || falha "nao consegui criar o arquivo (espaco?)"
        mkfs.ext4 -q -F -L BS-TRABALHO -E "root_owner=$(id -u):$(id -g)" "$BS_IMG" || { rm -f "$BS_IMG"; falha "mkfs.ext4 falhou"; }
    fi
    echo "== montando o ext4 de trabalho pelo udisks"
    loop=$(udisksctl loop-setup --no-user-interaction -f "$BS_IMG" 2>&1 | grep -o '/dev/loop[0-9]*' | head -1)
    if [ -n "$loop" ]; then
        sleep 2   # a area de trabalho as vezes monta sozinha
        [ -n "$(findmnt -n -o TARGET -S LABEL=BS-TRABALHO)" ] || udisksctl mount --no-user-interaction -b "$loop" >/dev/null 2>&1
    fi
    if [ -z "$(findmnt -n -o TARGET -S LABEL=BS-TRABALHO)" ]; then
        aviso "o udisks nao montou o arquivo; alternativa com sudo:"
        privilegiado mkdir -p /mnt/bs-trabalho && privilegiado mount -o loop "$BS_IMG" /mnt/bs-trabalho \
            || falha "ext4 de trabalho nao montado"
    fi
fi
trab=$(findmnt -n -o TARGET -S LABEL=BS-TRABALHO | head -1)
[ -w "$trab" ] || falha "$trab sem permissao de gravacao"
df -h "$trab" | tail -1
echo
echo "Pronto. Pasta de trabalho: $trab"
echo "Evidencias: $BS_EVID"
echo "Proximo: bash \"$BS_FERRAMENTAS/coletar.sh\" inicio   (antes de instalar qualquer pacote)"
