# N9.2 (docs/N9-2-PONTOS-DE-ENTRADA.md, secao 4): os pontos de entrada nativos da Remastered e o reinicio, sem janela, com o
# exe de VALIDACAO (BS_REINICIO_TESTE so existe nele) e os dados de uma Preview. Pastas de teste proprias; nada do usuario.
#
#   powershell -ExecutionPolicy Bypass -File tools/dist/test_remaster.ps1 [-Exe build\msvc-validacao\app\bluestinger.exe]
#       [-Data dist\BlueStinger-Preview\data] [-Save <BLUESTIN.000 VMS>] [-Work <pasta>]
#
#   R1  reinicio no processo (GameHost::restart) x processo novo: estado por passo identico
#   R2  NEW GAME nativo (eventos 794 menu, 994 novojogo) x entrada original (passo_newgame_area1): vida e posicao por passo
#       iguais e o filme NEWGAME no mesmo passo
#   R3  LOAD nativo (803 carregar 0) x LOAD original pelo painel (passo_load_slot1): o mesmo ponto e a mesma vida quando o
#       jogador aparece; o cartao nativo le o save uma vez so (sem o painel de VMU)
#   R4  na Remastered, um roteiro do menu original (sem eventos nativos) da o mesmo estado da Original (sem a camera: a
#       Remastered impoe a Livre), passo a passo: o menu original fica inteiro quando um roteiro o conduz
# Codigo de saida 0 = todos os casos passaram.
param(
    [string]$Exe = "",
    [string]$Data = "",
    [string]$Save = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Exe) { $Exe = Join-Path $repo "build\msvc-validacao\app\bluestinger.exe" }
if (-not $Data) { $Data = Join-Path $repo "dist\BlueStinger-Preview\data" }
if (-not $Save) { $Save = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n6f\jogo\g1\BLUESTIN.000" }
if (-not $Work) { $Work = Join-Path $env:LOCALAPPDATA "BlueStingerRecomp\n9-2" }
foreach ($f in @($Exe, (Join-Path $Data "disc"), $Save)) {
    if (-not (Test-Path -LiteralPath $f)) { Write-Host "ERRO: falta $f"; exit 2 }
}
$Work = Join-Path ([IO.Path]::GetFullPath($Work)) "test-remaster"
$marker = Join-Path $Work ".bs-test-remaster"
if (Test-Path -LiteralPath $Work) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Work existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Work
}
New-Item -ItemType Directory -Path $Work | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Name = $name; Ok = $ok }
    $tag = if ($ok) { "[ok]" } else { "[FALHA]" }
    Write-Host "$tag $name $detail"
}
# duas instalacoes do mesmo exe: Original (sem instalacao.toml) e Remastered
$orig = Join-Path $Work "original"; $rem = Join-Path $Work "remastered"
foreach ($d in @($orig, $rem)) { New-Item -ItemType Directory -Path $d | Out-Null; Copy-Item -LiteralPath $Exe -Destination $d }
[IO.File]::WriteAllText((Join-Path $rem "instalacao.toml"), "[edicao]`nperfil = `"remastered`"`n")

$vars = @("BS_STATE_PASSO", "BS_STATE_PASSO_CADA", "BS_STATE_PASSO_SEM_CAMERA", "BS_INPUT_SCRIPT", "BS_REMASTER_ACOES",
          "BS_REMASTER_PEDIR", "BS_REINICIO_TESTE")
function Jogo($dir, $vblanks, $env_, $extra = @()) {
    foreach ($v in $vars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
    foreach ($k in $env_.Keys) { Set-Item "Env:$k" $env_[$k] }
    $out = Join-Path $Work ("saida-" + [Guid]::NewGuid().ToString("N").Substring(0, 8) + ".txt")
    $argv = @("--data", "`"$Data`"", "--headless", "$vblanks") + $extra
    $p = Start-Process -FilePath (Join-Path $dir "bluestinger.exe") -ArgumentList $argv -NoNewWindow -Wait -PassThru `
        -RedirectStandardError $out -RedirectStandardOutput "$out.out"
    foreach ($v in $vars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
    return @{ Code = $p.ExitCode; Log = ([IO.File]::ReadAllText($out) + [IO.File]::ReadAllText("$out.out")) }
}
function Estado($tsv) {  # passo -> "estado|vida|x|y|z" (sem a coluna do VBlank)
    $m = @{}
    foreach ($l in ([IO.File]::ReadAllLines($tsv) | Select-Object -Skip 1)) {
        $c = $l.Split("`t")
        if ($c.Count -ge 7) { $m[[int]$c[0]] = ($c[2..6] -join "|") }
    }
    return $m
}
function Roteiro($nome, [string[]]$linhas) {
    $p = Join-Path $Work $nome
    [IO.File]::WriteAllText($p, (($linhas -join "`n") + "`n"))
    return $p
}
$rots = Join-Path $repo "tools\dist\roteiros"

# --- R1: reinicio ---
$a = Join-Path $Work "r1-novo.tsv"; $b = Join-Path $Work "r1-reiniciado.tsv"
$r = Jogo $rem 3000 @{ BS_STATE_PASSO = $a; BS_STATE_PASSO_CADA = "1" }
$r2 = Jogo $rem 3000 @{ BS_STATE_PASSO = $b; BS_STATE_PASSO_CADA = "1"; BS_REINICIO_TESTE = "1500" }
$ea = Estado $a; $eb = Estado $b
$igual = $ea.Count -gt 1000 -and $ea.Count -eq $eb.Count -and -not ($ea.Keys | Where-Object { $ea[$_] -ne $eb[$_] })
Check "R1 reinicio no processo x processo novo ($($ea.Count) passos)" ($r.Code -eq 0 -and $r2.Code -eq 0 -and $r2.Log -match '\[reinicio\]' -and $igual)

# --- R2: NEW GAME nativo x entrada original ---
$ng = Join-Path $rots "passo_newgame_area1.txt"
$soLogos = Roteiro "r2-logos.txt" (Get-Content $ng | Where-Object { $_ -notmatch '^(793|808|993|1008) ' })
$acoes = Roteiro "r2-acoes.txt" @("794 menu", "994 novojogo")
$a = Join-Path $Work "r2-original.tsv"; $b = Join-Path $Work "r2-nativo.tsv"
$ra = Jogo $rem 16000 @{ BS_STATE_PASSO = $a; BS_STATE_PASSO_CADA = "1"; BS_INPUT_SCRIPT = $ng }
$rb = Jogo $rem 16000 @{ BS_STATE_PASSO = $b; BS_STATE_PASSO_CADA = "1"; BS_INPUT_SCRIPT = $soLogos; BS_REMASTER_ACOES = $acoes }
$ea = Estado $a; $eb = Estado $b
$vp = { param($s) ($s.Split("|")[1..4]) -join "|" }  # vida e posicao
$difs = @($ea.Keys | Where-Object { -not $eb.ContainsKey($_) -or (& $vp $ea[$_]) -ne (& $vp $eb[$_]) })
$filmeA = [regex]::Match($ra.Log, 'NEWGAME\.SFD -> NEWGAME\.mp4 \(passo (\d+)\)').Groups[1].Value
$filmeB = [regex]::Match($rb.Log, 'NEWGAME\.SFD -> NEWGAME\.mp4 \(passo (\d+)\)').Groups[1].Value
Check "R2 NEW GAME nativo x original: vida e posicao em $($ea.Count) passos, filme no passo $filmeA / $filmeB" `
    ($ra.Code -eq 0 -and $rb.Code -eq 0 -and $ea.Count -gt 1000 -and $difs.Count -eq 0 -and $filmeA -and $filmeA -eq $filmeB) `
    "diferentes: $($difs.Count)"

# --- R3: LOAD nativo x LOAD original ---
function Cartao($nome) {
    $d = Join-Path $Work $nome
    $p = Start-Process -FilePath (Join-Path $rem "bluestinger.exe") -NoNewWindow -Wait -PassThru `
        -ArgumentList @("--data", "`"$Data`"", "--saves-pasta", "`"$d`"", "--saves-importar", "`"$Save`"", "--saves-nome", "BLUESTIN.000") `
        -RedirectStandardOutput (Join-Path $Work "$nome.txt") -RedirectStandardError (Join-Path $Work "$nome.err")
    if ($p.ExitCode -ne 0) { Write-Host "ERRO: importar o save falhou ($nome)"; exit 2 }
    return $d
}
$la = Roteiro "r3-load-original.txt" (Get-Content (Join-Path $rots "passo_load_slot1.txt") |
    Where-Object { $_ -match '^(#|indice)' -or ($_ -match '^(\d+) ' -and [int]$Matches[1] -le 1672) })
$lb = Roteiro "r3-logos.txt" @("indice passo", "1 START", "4 -", "602 START", "605 -")
$acoes = Roteiro "r3-acoes.txt" @("803 carregar 0")
$ca = Cartao "r3-cartao-original"; $cb = Cartao "r3-cartao-nativo"
$a = Join-Path $Work "r3-original.tsv"; $b = Join-Path $Work "r3-nativo.tsv"
$ra = Jogo $rem 6000 @{ BS_STATE_PASSO = $a; BS_STATE_PASSO_CADA = "1"; BS_INPUT_SCRIPT = $la } @("--saves-pasta", "`"$ca`"")
$rb = Jogo $rem 6000 @{ BS_STATE_PASSO = $b; BS_STATE_PASSO_CADA = "1"; BS_INPUT_SCRIPT = $lb; BS_REMASTER_ACOES = $acoes } @("--saves-pasta", "`"$cb`"")
function Aparece($tsv) {  # o primeiro passo com o jogador no mapa: "vida|x|y|z"
    foreach ($l in ([IO.File]::ReadAllLines($tsv) | Select-Object -Skip 1)) {
        $c = $l.Split("`t")
        if ($c[4] -ne "0.000" -or $c[6] -ne "0.000") { return @{ Passo = [int]$c[0]; Vp = ($c[3..6] -join "|") } }
    }
    return $null
}
$pa = Aparece $a; $pb = Aparece $b
$loads = [regex]::Match($rb.Log, 'LOAD (\d+) \(ok (\d+)').Groups[1].Value
Check "R3 LOAD nativo x original: o jogador aparece igual ($($pa.Vp)) no passo $($pa.Passo) / $($pb.Passo); leituras do save: $loads" `
    ($pa -and $pb -and $pa.Vp -eq $pb.Vp -and $pb.Passo -lt $pa.Passo -and $loads -eq "1" -and $rb.Log -match '\[remaster\] carregar 0')

# --- R4: roteiro do menu original na Remastered x Original ---
$a = Join-Path $Work "r4-original.tsv"; $b = Join-Path $Work "r4-remastered.tsv"
$ra = Jogo $orig 4000 @{ BS_STATE_PASSO = $a; BS_STATE_PASSO_CADA = "1"; BS_STATE_PASSO_SEM_CAMERA = "1"; BS_INPUT_SCRIPT = $ng }
$rb = Jogo $rem 4000 @{ BS_STATE_PASSO = $b; BS_STATE_PASSO_CADA = "1"; BS_STATE_PASSO_SEM_CAMERA = "1"; BS_INPUT_SCRIPT = $ng }
$ea = Estado $a; $eb = Estado $b
$difs = @($ea.Keys | Where-Object { $ea[$_] -ne $eb[$_] })
Check "R4 roteiro do menu original: Remastered = Original sem a camera ($($ea.Count) passos)" `
    ($ra.Code -eq 0 -and $rb.Code -eq 0 -and $ea.Count -gt 900 -and $ea.Count -eq $eb.Count -and $difs.Count -eq 0) "diferentes: $($difs.Count)"

$falhas = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ""
Write-Host "$($results.Count) casos, $falhas falhas"
exit ([int]($falhas -ne 0))
