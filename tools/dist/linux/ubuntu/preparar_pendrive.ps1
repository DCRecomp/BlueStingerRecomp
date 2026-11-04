# N9-L: monta no Windows o disco de troca da sessao em Ubuntu nativo (docs/N9-L-ROTEIRO-UBUNTU.md, secao 2).
#
#   powershell -ExecutionPolicy Bypass -File tools\dist\linux\ubuntu\preparar_pendrive.ps1 -Destino E:\ [-Pacote <tar.gz>]
#
# Cria <Destino>\BS-TROCA com:
#   publico\   o pacote .tar.gz da L5 e o .sha256; ferramentas\ (estes scripts, a suite do Builder e os roteiros, tirados
#              do commit, nao da arvore de trabalho); IDENTIFICACAO.txt (commit, arvore e hashes)
#   privado\   o seu CDI, os dados e o C++ gerado do Windows (A2), o E017.mp4 (mod), as legendas locais e a sessao gravada
#              com o E016 e o E017. Conteudo do jogo e seu: NUNCA publicar.
#   SHA256SUMS-TROCA.txt  conferido no Ubuntu antes de tudo
# So grava no destino. O destino tem de ser um disco USB (pendrive ou SSD externo) com exFAT ou NTFS; -Teste aceita
# qualquer pasta, para testar o script. Nao formata nada e nao apaga nada: se <Destino>\BS-TROCA ja existir, para; com
# -Substituir, renomeia a sessao anterior para BS-TROCA.anterior-<data> e cria outra.
param(
    [Parameter(Mandatory = $true)][string]$Destino,
    [string]$Pacote = "\\wsl.localhost\Ubuntu\home\rodrigo\bs-linux\l5\pacote-l5zig6\BlueStinger-Builder-l5zig6-linux-x86_64.tar.gz",
    [switch]$Teste,
    [switch]$Substituir
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..\..\..").Path
$env:GIT_CONFIG_COUNT = "1"; $env:GIT_CONFIG_KEY_0 = "safe.directory"; $env:GIT_CONFIG_VALUE_0 = "*"
function Pare($m) { Write-Host "ERRO: $m" -ForegroundColor Red; exit 1 }

# 1. destino: disco USB, sistema de arquivos que aceita o arquivo de trabalho de 20 GB
if (-not (Test-Path -LiteralPath $Destino -PathType Container)) { Pare "destino $Destino nao existe" }
$dest = (Resolve-Path -LiteralPath $Destino).Path
$letra = $dest.Substring(0, 1)
$vol = Get-Volume -DriveLetter $letra
if (-not $Teste) {
    $disco = Get-Partition -DriveLetter $letra | Get-Disk
    if ($disco.BusType -ne "USB") { Pare "$($letra): esta num disco $($disco.BusType) ($($disco.FriendlyName)); use um disco USB (ou -Teste)" }
    if ($vol.FileSystem -notin @("exFAT", "NTFS")) { Pare "$($letra): e $($vol.FileSystem); precisa de exFAT ou NTFS (o FAT32 nao aceita o arquivo de trabalho de 20 GB)" }
    Write-Host ("Destino: {0}: {1} ({2}, {3}, {4:N0} GB livres)" -f $letra, $disco.FriendlyName, $vol.FileSystem, $vol.FileSystemLabel, ($vol.SizeRemaining / 1GB))
}
$livreGB = [math]::Floor($vol.SizeRemaining / 1GB)
if ($livreGB -lt 25) { Write-Host "AVISO: $livreGB GB livres; a sessao precisa de uns 3 GB de copia e 20 GB do arquivo de trabalho" -ForegroundColor Yellow }

# 2. ferramentas do commit: os arquivos tem de estar commitados (o que vai e o que foi revisado)
$caminhos = @("tools/dist/linux/ubuntu", "tools/dist/linux/test_builder_linux.py", "tools/dist/roteiros/passo_newgame_area1.txt")
$sujos = git -C $repo status --porcelain -- $caminhos
if ($sujos -and -not $Teste) { Pare "ferramentas com mudancas sem commit:`n$sujos" }
$commit = (git -C $repo rev-parse HEAD).Trim()
$arvore = (git -C $repo rev-parse "HEAD^{tree}").Trim()

# 3. pacote
if (-not (Test-Path -LiteralPath $Pacote)) { Pare "pacote $Pacote nao encontrado" }
if (-not (Test-Path -LiteralPath "$Pacote.sha256")) { Pare "falta $Pacote.sha256" }
$pacSha = (Get-FileHash -LiteralPath $Pacote -Algorithm SHA256).Hash.ToLower()
$pacEsp = ((Get-Content -LiteralPath "$Pacote.sha256" -Raw) -split '\s+')[0].ToLower()
if ($pacSha -ne $pacEsp) { Pare "SHA-256 do pacote nao confere com o .sha256" }

$raiz = Join-Path $dest "BS-TROCA"
$pub = Join-Path $raiz "publico"; $priv = Join-Path $raiz "privado"
# uma sessao anterior (com privado\, trabalho.img e evidencias) nunca e apagada: so renomeada, com -Substituir
if (Test-Path -LiteralPath $raiz) {
    $antiga = "$raiz.anterior-$(Get-Date -Format yyyyMMdd-HHmmss)"
    if (-not $Substituir) { Pare "$raiz ja existe (sessao anterior). Rode com -Substituir para renomea-la para $(Split-Path $antiga -Leaf) e criar outra; nada e apagado" }
    Rename-Item -LiteralPath $raiz -NewName (Split-Path $antiga -Leaf)
    Write-Host "sessao anterior renomeada: $antiga (apague-a voce, quando quiser)"
}
New-Item -ItemType Directory -Force $pub, $priv, "$pub\ferramentas" | Out-Null

Write-Host "== publico"
Copy-Item -LiteralPath $Pacote, "$Pacote.sha256" -Destination $pub
$tmpTar = Join-Path $env:TEMP "bs-ferramentas-$PID.tar"
if ($Teste -and $sujos) {
    foreach ($c in $caminhos) {
        $alvo = Join-Path "$pub\ferramentas" ($c -replace '^tools/dist/linux/', '' -replace '^tools/dist/', '')
        New-Item -ItemType Directory -Force (Split-Path $alvo) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repo $c) -Destination $alvo -Recurse -Force
    }
} else {
    git -C $repo archive --format=tar -o $tmpTar HEAD -- $caminhos
    New-Item -ItemType Directory -Force "$env:TEMP\bs-ferramentas-$PID" | Out-Null
    tar -xf $tmpTar -C "$env:TEMP\bs-ferramentas-$PID"
    Copy-Item "$env:TEMP\bs-ferramentas-$PID\tools\dist\linux\ubuntu" "$pub\ferramentas\ubuntu" -Recurse
    Copy-Item "$env:TEMP\bs-ferramentas-$PID\tools\dist\linux\test_builder_linux.py" "$pub\ferramentas\"
    Copy-Item "$env:TEMP\bs-ferramentas-$PID\tools\dist\roteiros" "$pub\ferramentas\roteiros" -Recurse
    Remove-Item -LiteralPath $tmpTar, "$env:TEMP\bs-ferramentas-$PID" -Recurse -Force
}
Remove-Item -LiteralPath "$pub\ferramentas\ubuntu\preparar_pendrive.ps1" -ErrorAction SilentlyContinue

$base = (Split-Path $Pacote -Leaf) -replace '\.tar\.gz$', ''
$tarArgs = @("-xzOf", $Pacote, "$base/VERSAO.txt")   # argumentos separados: nada do caminho vira comando
$versao = (& tar.exe @tarArgs) -join "`n"
if ($LASTEXITCODE -ne 0 -or -not $versao) { Pare "nao consegui ler o VERSAO.txt do pacote" }
$ident = @"
Sessao de validacao N9-L (L3, L4 e L5) em Ubuntu nativo
Preparada em: $(Get-Date -Format "yyyy-MM-dd HH:mm")
Commit das ferramentas: $commit
Arvore do commit: $arvore
Pacote: $(Split-Path $Pacote -Leaf)
SHA-256 do pacote: $pacSha
O pacote foi montado da arvore 72132c1a (a da suite B1-B14 108/0 e da A3); ela so difere do commit acima no STATUS.md e no
docs/N9-L-PLANO.md, que nao vao no pacote (STATUS.md, secao da L5).
--- VERSAO.txt do pacote
$versao
"@
Set-Content -LiteralPath "$pub\IDENTIFICACAO.txt" -Value $ident -Encoding UTF8

Write-Host "== privado (conteudo do jogo e dados seus: nao publicar)"
$cdi = Join-Path $repo "ISO\Blue Stinger.cdi"
$prev = Join-Path $repo "dist\BlueStinger-Preview"
$grav = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\gravacoes\sessao-20261021-145006"
foreach ($x in @($cdi, "$prev\data\disc", "$prev\data\video\manifest.toml", "$prev\data\video\E017.mp4", "$repo\generated", $grav)) {
    if (-not (Test-Path -LiteralPath $x)) { Pare "falta $x" }
}
Copy-Item -LiteralPath $cdi -Destination $priv
New-Item -ItemType Directory -Force "$priv\win-data\video", "$priv\gravacoes\sessao-20261021-145006" | Out-Null
Copy-Item -LiteralPath "$prev\data\disc" -Destination "$priv\win-data" -Recurse
Copy-Item -LiteralPath "$prev\data\disc.manifest", "$prev\data\disc_meta.bin", "$prev\data\disc_meta.idx" -Destination "$priv\win-data"
Copy-Item -LiteralPath "$prev\data\video\manifest.toml" -Destination "$priv\win-data\video"
Copy-Item -LiteralPath "$repo\generated" -Destination "$priv\win-generated" -Recurse
Copy-Item -LiteralPath "$prev\data\video\E017.mp4" -Destination "$priv\mod-E017.mp4"
if (Test-Path -LiteralPath "$prev\mods") {
    New-Item -ItemType Directory -Force "$priv\legendas" | Out-Null
    Get-ChildItem -LiteralPath "$prev\mods" -Directory | Where-Object { $_.Name -like "legendas-*" } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$priv\legendas" -Recurse }
}
foreach ($f in @("entrada.txt", "sessao.json", "saves_inicio")) { Copy-Item -LiteralPath "$grav\$f" -Destination "$priv\gravacoes\sessao-20261021-145006" -Recurse }
Set-Content -LiteralPath "$priv\LEIA-PRIMEIRO.txt" -Encoding UTF8 -Value @"
Esta pasta tem conteudo do jogo (CDI, dados, C++ gerado, filme) e dados seus (saves, sessao gravada, legendas transcritas).
Serve so para a sessao de validacao. Nunca publicar, anexar a issue, release ou commit. Apague quando terminar.
"@
$cdiDest = (Get-FileHash -LiteralPath "$priv\Blue Stinger.cdi" -Algorithm SHA256).Hash
if ($cdiDest -ne (Get-FileHash -LiteralPath $cdi -Algorithm SHA256).Hash) { Pare "a copia do CDI nao confere" }

Write-Host "== SHA256SUMS-TROCA.txt"
$linhas = Get-ChildItem -LiteralPath $pub, $priv -Recurse -File | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($raiz.Length + 1) -replace '\\', '/'
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(), $rel
}
[IO.File]::WriteAllText((Join-Path $raiz "SHA256SUMS-TROCA.txt"), (($linhas -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
# os scripts .sh com fim de linha do Linux (o git do Windows pode ter convertido)
Get-ChildItem -LiteralPath "$pub\ferramentas" -Recurse -File -Include *.sh, *.py, *.txt | ForEach-Object {
    $t = [IO.File]::ReadAllText($_.FullName)
    if ($t.Contains("`r`n")) { Pare "$($_.Name) com CRLF: confira o .gitattributes" }
}
$tam = (Get-ChildItem -LiteralPath $raiz -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Pronto: {0} ({1:N0} MB, {2} arquivos conferidos)" -f $raiz, $tam, $linhas.Count)
Write-Host "No Ubuntu: bash /media/`$USER/<rotulo do disco>/BS-TROCA/publico/ferramentas/ubuntu/preparar_sessao.sh"
