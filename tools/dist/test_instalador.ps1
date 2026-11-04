# N9.1 (docs/N9-1-INSTALADOR.md, secao 5): o instalador de ponta a ponta, com o CDI real, numa pasta isolada. A integracao com
# o sistema (atalhos, "Aplicativos instalados", lista das instalacoes, preferencias) vai para BS_INSTALADOR_TESTE_RAIZ: o menu,
# a area de trabalho e o registro reais nao sao tocados.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_instalador.ps1 -Disc <imagem.cdi> [-Build build\msvc-release] [-Work <pasta>]
#
#   I1  instalacao limpa da Remastered so pelo teclado (roteiro instalar.txt): edicao, legendas, desinstalador no pacote,
#       atalho, entrada de desinstalacao, lista, progresso sem recuo
#   I2  dados pessoais de referencia (saves, preferencias, mods, arquivo do jogador) e o hash deles
#   I3  cancelamento em cada fase (extracao, preparacao, instalacao): codigo 3 e a instalacao anterior intacta e utilizavel
#   I4  atualizacao para a Original: dados pessoais iguais, perfil trocado, anterior guardada
#   I5  verificacao pela tela (teclado): integra; com um arquivo adulterado, acusa
#   I6  desinstalacao pelo BlueStinger-Desinstalar do pacote (--auto): so o pacote sai (o desinstalador e o runtime logo
#       depois do processo); dados pessoais iguais; atalho, entrada e lista removidos
#   I7  instalacao limpa da Original (--auto --integrar) e desinstalacao sem nada do jogador, com & % ^ e Unicode no nome da
#       pasta: a pasta some
# O kit de fontes e o HEAD exportado (git archive). Codigo de saida 0 = todos os casos passaram. Leva de 20 a 40 minutos
# (duas compilacoes completas).
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [string]$Build = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Build) { $Build = Join-Path $repo "build\msvc-release" }
if (-not $Work) { $Work = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n9-1" }
$Disc = (Resolve-Path -LiteralPath $Disc).Path
$rec = Join-Path $Build "builder\BlueStinger-Recompiler.exe"
$sdl3 = Join-Path $Build "_deps\sdl3-src"
foreach ($f in @($rec, (Join-Path $Build "builder\BlueStinger-Desinstalar.exe"), (Join-Path $sdl3 "CMakeLists.txt"))) {
    if (-not (Test-Path -LiteralPath $f)) { Write-Host "ERRO: falta $f"; exit 2 }
}

$Work = Join-Path ([IO.Path]::GetFullPath($Work)) "teste-instalador"
$marker = Join-Path $Work ".bs-instalador-teste"
if (Test-Path -LiteralPath $Work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Work existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Work
}
New-Item -ItemType Directory -Path $Work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null
$raiz = Join-Path $Work "raiz"
$env:BS_INSTALADOR_TESTE_RAIZ = $raiz
$regTeste = "HKCU:\Software\BlueStingerRecomp\Teste\Uninstall"
if (Test-Path $regTeste) { Remove-Item -Recurse -Force $regTeste }

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function Run($exe, [string[]]$argv, $timeoutMs = 3600000) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $exe
    $psi.Arguments = ($argv | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.Encoding]::UTF8
    $psi.WorkingDirectory = $env:SystemRoot
    $t0 = Get-Date
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($timeoutMs)) { $p.Kill(); return @{ Code = -1; Out = "tempo esgotado"; Secs = 0 } }
    $p.WaitForExit()
    return @{ Code = $p.ExitCode; Out = $o.Result + $e.Result; Secs = [int]((Get-Date) - $t0).TotalSeconds }
}
function Hash($f) { if (Test-Path -LiteralPath $f) { (Get-FileHash -LiteralPath $f).Hash } else { "" } }
# hash de cada arquivo do jogador (fora da lista do pacote), por caminho relativo
function Pessoais($jogo) {
    $lista = @{}
    foreach ($l in [IO.File]::ReadAllLines((Join-Path $jogo ".bs-package"))) { if ($l) { $lista[$l.ToLowerInvariant()] = 1 } }
    $r = [ordered]@{}
    Get-ChildItem -LiteralPath $jogo -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($jogo.Length + 1).Replace('\', '/')
        # o instalacao.toml e da instalacao (o instalador o escreve, N9.0): fica fora da comparacao
        if (-not $lista.ContainsKey($rel.ToLowerInvariant()) -and $rel -ne "instalacao.toml") { $r[$rel] = (Get-FileHash -LiteralPath $_.FullName).Hash }
    }
    return ($r.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`n"
}
function Roteiro($nome, $troca) {
    $t = [IO.File]::ReadAllText((Join-Path $repo "tools\dist\roteiros-instalador\$nome"))
    foreach ($k in $troca.Keys) { $t = $t.Replace($k, $troca[$k]) }
    $f = Join-Path $Work $nome
    [IO.File]::WriteAllText($f, $t)
    return $f
}
function Responde($jogo) {
    $exe = Join-Path $jogo "BlueStinger.exe"
    $v = Run $exe @("--version") 60000
    $d = Run $exe @("--verify-data") 600000
    return ($v.Code -eq 0 -and $d.Code -eq 0)
}
# kit de fontes: o HEAD exportado (git archive), como o de uma distribuicao (so arquivos versionados; nada particular da
# arvore de trabalho entra na compilacao). O N9.1 nao muda nada do que o jogo compila.
$kitDir = Join-Path $Work "kit"
New-Item -ItemType Directory -Path $kitDir | Out-Null
$tar = Join-Path $Work "kit.tar"
& git -c "safe.directory=$($repo.Replace('\', '/'))" -C $repo archive --format=tar -o $tar HEAD
if ($LASTEXITCODE -ne 0) { Write-Host "ERRO: git archive"; exit 2 }
& (Join-Path $env:SystemRoot "System32\tar.exe") -xf $tar -C $kitDir  # o do Windows (o GNU tar le "C:" como maquina remota)
if ($LASTEXITCODE -ne 0) { Write-Host "ERRO: tar"; exit 2 }
Remove-Item -LiteralPath $tar
$kit = @("--kit", $kitDir, "--sdl3", $sdl3)

# --- I1: instalacao limpa da Remastered, so pelo teclado ---
$pastaA = Join-Path $Work "Jogo A"
$jogoA = Join-Path $pastaA "BlueStinger"
$rot = Roteiro "instalar.txt" @{ "@CDI@" = $Disc; "@PASTA@" = $pastaA }
$r = Run $rec (@("--teclas", $rot, "--captura", (Join-Path $Work "capturas-I1"), "--idioma", "pt-BR", "--sem-filmes") + $kit)
[IO.File]::WriteAllText((Join-Path $Work "I1.saida.txt"), $r.Out)
Check "I1 instalacao Remastered pelo teclado ate a conclusao ($($r.Secs) s)" ($r.Code -eq 0) ($r.Out -split "`n" | Select-Object -Last 3)
$cfg = if (Test-Path -LiteralPath (Join-Path $jogoA "instalacao.toml")) { [IO.File]::ReadAllText((Join-Path $jogoA "instalacao.toml")) } else { "" }
Check "I1 instalacao.toml: perfil remastered e legendas pt-BR" ($cfg -match 'perfil = "remastered"' -and $cfg -match 'idioma = "pt-BR"') $cfg.Replace("`n", " ")
$pkg = if (Test-Path -LiteralPath (Join-Path $jogoA ".bs-package")) { [IO.File]::ReadAllText((Join-Path $jogoA ".bs-package")) } else { "" }
$sha = if (Test-Path -LiteralPath (Join-Path $jogoA ".bs-package.sha256")) { [IO.File]::ReadAllText((Join-Path $jogoA ".bs-package.sha256")) } else { "" }
Check "I1 desinstalador no pacote, na lista e nos hashes" ((Test-Path -LiteralPath (Join-Path $jogoA "BlueStinger-Desinstalar.exe")) -and $pkg -match "BlueStinger-Desinstalar.exe" -and $sha -match "BlueStinger-Desinstalar.exe" -and $sha -notmatch "  data/")
$lnk = Join-Path $raiz "menu\Blue Stinger Remastered.lnk"
Check "I1 atalho do Menu Iniciar (na raiz de teste)" (Test-Path -LiteralPath $lnk)
$ent = @(Get-ChildItem $regTeste -ErrorAction SilentlyContinue)
$cmd = if ($ent.Count -eq 1) { (Get-ItemProperty $ent[0].PSPath).UninstallString } else { "" }
Check "I1 entrada de desinstalacao chama o desinstalador do pacote" ($ent.Count -eq 1 -and $cmd -match [regex]::Escape((Join-Path $jogoA "BlueStinger-Desinstalar.exe")) -and $cmd -match "--pasta") $cmd
$lista = Join-Path $raiz "usuario\instalacoes.toml"
Check "I1 lista das instalacoes do usuario" ((Test-Path -LiteralPath $lista) -and ([IO.File]::ReadAllText($lista)).Contains($pastaA.Replace('\', '/')))
Check "I1 o jogo instalado responde e confere os dados" (Responde $jogoA)

# --- I2: dados pessoais ---
New-Item -ItemType Directory -Force -Path (Join-Path $jogoA "saves"), (Join-Path $jogoA "mods\meu-mod") | Out-Null
[IO.File]::WriteAllText((Join-Path $jogoA "saves\BLUESTIN.000"), "save do jogador")
[IO.File]::WriteAllText((Join-Path $jogoA "controles.toml"), "[camera]`nsensibilidade = 1.5`n")
[IO.File]::WriteAllText((Join-Path $jogoA "graficos.toml"), "[graficos]`nresolucao = 2`n")
[IO.File]::WriteAllText((Join-Path $jogoA "mods\meu-mod\mod.toml"), "[mod]`nid = `"meu-mod`"`n")
[IO.File]::WriteAllText((Join-Path $jogoA "anotacoes do jogador.txt"), "nao apagar")
$pessoais = Pessoais $jogoA
Check "I2 dados pessoais de referencia" (($pessoais -split "`n").Count -ge 5) "$((($pessoais -split "`n").Count)) arquivos"
$exeA = Hash (Join-Path $jogoA "BlueStinger.exe")
$marcaA = [IO.File]::ReadAllText((Join-Path $jogoA ".bs-install"))

# --- I3: cancelamento em cada fase ---
foreach ($fase in @("extracao", "preparacao", "instalacao")) {
    $r = Run $rec (@("--disc", $Disc, "--out", $pastaA, "--auto", "--sem-filmes", "--idioma", "en-US", "--cancelar-na-fase", $fase) + $kit)
    $intacta = (Hash (Join-Path $jogoA "BlueStinger.exe")) -eq $exeA -and [IO.File]::ReadAllText((Join-Path $jogoA ".bs-install")) -eq $marcaA
    $semSobra = @(Get-ChildItem -LiteralPath $pastaA -Directory -Force | Where-Object { $_.Name -like "BlueStinger.new-*" -or $_.Name -like "BlueStinger.old-*" }).Count -eq 0
    Check "I3 cancelar na $fase`: codigo 3, anterior intacta, sem sobras ($($r.Secs) s)" ($r.Code -eq 3 -and $intacta -and $semSobra) "codigo $($r.Code)"
    Check "I3 depois de cancelar na $fase`: dados pessoais iguais e o jogo responde" ((Pessoais $jogoA) -eq $pessoais -and (Responde $jogoA))
}

# --- I4: atualizacao para a Original ---
$r = Run $rec (@("--disc", $Disc, "--out", $pastaA, "--auto", "--sem-filmes", "--edicao", "original", "--integrar") + $kit)
$cfg = [IO.File]::ReadAllText((Join-Path $jogoA "instalacao.toml"))
Check "I4 atualizacao para a Original ($($r.Secs) s)" ($r.Code -eq 0 -and $cfg -match 'perfil = "original"' -and $cfg -match 'idioma = "pt-BR"') $cfg.Replace("`n", " ")
$depois = Pessoais $jogoA
Check "I4 dados pessoais identicos depois da atualizacao" ($depois -eq $pessoais) ""
Check "I4 versao anterior guardada e o jogo novo responde" ((Test-Path -LiteralPath (Join-Path $pastaA "BlueStinger.anterior\.bs-install")) -and (Responde $jogoA))
Check "I4 atalho refeito com o nome da Original" ((Test-Path -LiteralPath (Join-Path $raiz "menu\Blue Stinger.lnk")) -and -not (Test-Path -LiteralPath $lnk))

# --- I5: verificacao pela tela ---
$rot = Roteiro "verificar.txt" @{ "@ESPERADO@" = "1" }
$r = Run $rec (@("--teclas", $rot, "--captura", (Join-Path $Work "capturas-I5")) + $kit) 1200000
Check "I5 verificacao pelo teclado: instalacao integra" ($r.Code -eq 0) ($r.Out -split "`n" | Select-Object -Last 2)
$lic = Get-ChildItem -LiteralPath (Join-Path $jogoA "licencas") -File | Select-Object -First 1
$orig = [IO.File]::ReadAllBytes($lic.FullName)
[IO.File]::AppendAllText($lic.FullName, "adulterado")
$rot = Roteiro "verificar.txt" @{ "@ESPERADO@" = "0" }
$r = Run $rec (@("--teclas", $rot, "--captura", (Join-Path $Work "capturas-I5b")) + $kit) 1200000
Check "I5 verificacao acusa o arquivo adulterado ($($lic.Name))" ($r.Code -eq 0) ($r.Out -split "`n" | Select-Object -Last 2)
[IO.File]::WriteAllBytes($lic.FullName, $orig)

# --- I6: desinstalacao pelo desinstalador do pacote ---
$des = Join-Path $jogoA "BlueStinger-Desinstalar.exe"
$r = Run $des @("--pasta", $pastaA, "--auto") 600000
# o desinstalador e as DLLs do runtime que ele carregou saem logo depois dele (cmd que espera o processo)
$t0 = Get-Date
while ((Test-Path -LiteralPath $des) -and ((Get-Date) - $t0).TotalSeconds -lt 30) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 1
$sobra = @(Get-ChildItem -LiteralPath $jogoA -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName.Substring($jogoA.Length + 1) } | Where-Object { $_ -ne "instalacao.toml" })
Check "I6 desinstalacao --auto: codigo 0, sem o executavel e os dados" ($r.Code -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $jogoA "BlueStinger.exe")) -and -not (Test-Path -LiteralPath (Join-Path $jogoA "data"))) "codigo $($r.Code)"
$depois = @()
foreach ($l in ($pessoais -split "`n")) {
    $rel, $h = $l -split "=", 2
    if ((Hash (Join-Path $jogoA $rel.Replace('/', '\'))) -ne $h) { $depois += $rel }
}
Check "I6 dados pessoais identicos depois da desinstalacao" ($depois.Count -eq 0) ($depois -join ", ")
Check "I6 so ficaram arquivos do jogador" ($sobra.Count -eq ($pessoais -split "`n").Count) "$($sobra.Count) arquivos"
Check "I6 atalho, entrada e lista removidos" (-not (Test-Path -LiteralPath (Join-Path $raiz "menu\Blue Stinger.lnk")) -and @(Get-ChildItem $regTeste -ErrorAction SilentlyContinue).Count -eq 0 -and -not ([IO.File]::ReadAllText($lista)).Contains($pastaA.Replace('\', '/')))
Check "I6 trabalho do Builder removido (.bs-build)" (-not (Test-Path -LiteralPath (Join-Path $pastaA ".bs-build")))
Check "I6 o desinstalador e o runtime sairam depois do processo" (-not (Test-Path -LiteralPath $des) -and -not (Test-Path -LiteralPath (Join-Path $jogoA "vcruntime140.dll")))

# --- I7: instalacao limpa da Original e desinstalacao sem nada do jogador ---
$pastaB = Join-Path $Work "Jogo B"
$r = Run $rec (@("--disc", $Disc, "--out", $pastaB, "--auto", "--sem-filmes", "--edicao", "original", "--integrar") + $kit)
$cfg = if (Test-Path -LiteralPath (Join-Path $pastaB "BlueStinger\instalacao.toml")) { [IO.File]::ReadAllText((Join-Path $pastaB "BlueStinger\instalacao.toml")) } else { "" }
Check "I7 instalacao limpa da Original ($($r.Secs) s)" ($r.Code -eq 0 -and $cfg -match 'perfil = "original"' -and (Responde (Join-Path $pastaB "BlueStinger"))) "codigo $($r.Code)"
Get-ChildItem -LiteralPath (Join-Path $pastaB "BlueStinger") -Filter "*.log" -ErrorAction SilentlyContinue | Remove-Item -Force
# a pasta passa a ter & % ^ ( ) e Unicode no nome (revisao Codex 20261104-195545): o cmd que apaga o desinstalador depois da
# saida nao pode interpretar o nome; se interpretasse, a pasta ficaria (e %PATH% viraria outro caminho)
$nomeC = "JogoB&x%PATH%^(1)" + [char]0xE7 + [char]0x65E5 + [char]0x672C
Rename-Item -LiteralPath $pastaB -NewName $nomeC
$pastaB = Join-Path $Work $nomeC
$r = Run (Join-Path $pastaB "BlueStinger\BlueStinger-Desinstalar.exe") @("--pasta", $pastaB, "--auto") 600000
$t0 = Get-Date
while ((Test-Path -LiteralPath $pastaB) -and ((Get-Date) - $t0).TotalSeconds -lt 30) { Start-Sleep -Milliseconds 500 }
Check "I7 desinstalacao sem arquivos do jogador remove a pasta (nome com & % ^ e Unicode)" ($r.Code -eq 0 -and -not (Test-Path -LiteralPath $pastaB)) "codigo $($r.Code)"

if (Test-Path $regTeste) { Remove-Item -Recurse -Force $regTeste }
$falhas = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("`n{0} casos, {1} falhas" -f $results.Count, $falhas)
exit ([int]($falhas -ne 0))
