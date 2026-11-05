# N9.0 (docs/N9-0-EDICOES.md): as duas edicoes no mesmo executavel. O perfil vem do instalacao.toml ao lado do exe, e so
# dele. Cada caso roda numa copia da Preview por hard links (a pasta do exe tambem e a das preferencias), entao nada da
# Preview nem do usuario e tocado. A pasta "remastered" fica pronta para `test_campanha.py --casos C8 --outra <pasta>`.
#   powershell -File tools/dist/test_edicao.ps1 [-Preview dist\BlueStinger-Preview] [-Saida <pasta>]
param(
    [string]$Preview = (Join-Path $PSScriptRoot "..\..\dist\BlueStinger-Preview"),
    [string]$Saida = (Join-Path $env:TEMP "bs-edicao-teste")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath (Join-Path $Preview "BlueStinger.exe"))) { Write-Host "SKIP: Preview ausente ($Preview)"; exit 0 }
$Preview = (Resolve-Path -LiteralPath $Preview).Path
$marker = Join-Path $Saida ".bs-edicao-teste"
if (Test-Path -LiteralPath $Saida) {
    if (-not (Test-Path -LiteralPath $marker)) { Write-Host "ERRO: $Saida existe e nao foi criada por este teste"; exit 2 }
    Remove-Item -Recurse -Force -LiteralPath $Saida
}
New-Item -ItemType Directory -Path $Saida | Out-Null
New-Item -ItemType File -Path $marker | Out-Null

$results = @()
function Check($name, [bool]$ok, $detail = "") {
    $script:results += [pscustomobject]@{ Caso = $name; Ok = $ok }
    Write-Host ("[{0}] {1} {2}" -f ($(if ($ok) { "ok" } else { "FALHA" })), $name, $detail)
}
function New-Instalacao($nome, $toml) {
    $d = Join-Path $Saida $nome
    foreach ($f in Get-ChildItem -LiteralPath $Preview -Recurse -File) {
        if ($f.Name -in "instalacao.toml", "controles.toml", "graficos.toml", "audio.toml", "legendas.toml", "bluestinger.log") { continue }
        $alvo = Join-Path $d $f.FullName.Substring($Preview.Length + 1)
        New-Item -ItemType Directory -Force -Path (Split-Path $alvo) | Out-Null
        New-Item -ItemType HardLink -Path $alvo -Target $f.FullName | Out-Null
    }
    if ($toml) { [IO.File]::WriteAllText((Join-Path $d "instalacao.toml"), $toml) }
    return $d
}
# um processo headless curto; devolve o codigo e a saida
function Rodar($dir, $argumentos, $espera = 60000) {
    $o = Join-Path $Saida ("saida-{0}.txt" -f [guid]::NewGuid().ToString("N"))
    $p = Start-Process -FilePath (Join-Path $dir "BlueStinger.exe") -ArgumentList $argumentos -WorkingDirectory $dir -PassThru `
        -RedirectStandardError $o -RedirectStandardOutput "$o.out" -WindowStyle Hidden
    $null = $p.Handle  # sem guardar o handle, o PowerShell 5.1 perde o ExitCode
    $fim = $p.WaitForExit($espera)
    if (-not $fim) { Stop-Process -Id $p.Id -Force }
    $texto = (Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue) + (Get-Content -LiteralPath "$o.out" -Raw -ErrorAction SilentlyContinue)
    return [pscustomobject]@{ Fim = $fim; Rc = $p.ExitCode; Texto = "$texto" }
}
$headless = "--headless 200"
$original = New-Instalacao "original" $null
$remaster = New-Instalacao "remastered" "[edicao]`nperfil = `"remastered`"`n"
$invalida = New-Instalacao "invalida" "[edicao]`nperfil = `"livre`"`n"

$hashes = @($original, $remaster, $invalida | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $_ "BlueStinger.exe")).Hash } | Sort-Object -Unique)
Check "E0 o mesmo executavel nas tres pastas" ($hashes.Count -eq 1) $hashes[0].Substring(0, 12)

$r = Rodar $original $headless
Check "E1 sem [edicao]: Original, sem camera livre" ($r.Fim -and $r.Rc -eq 0 -and $r.Texto -match '\[edicao\] perfil original' -and $r.Texto -notmatch 'modo livre') "rc=$($r.Rc)"

$r = Rodar $remaster $headless
Check "E2 Remastered: camera livre sem pedir" ($r.Fim -and $r.Rc -eq 0 -and $r.Texto -match '\[edicao\] perfil remastered' -and $r.Texto -match '\[camera\] modo livre') "rc=$($r.Rc)"

$r = Rodar $remaster "$headless --camera original"
Check "E3 Remastered: --camera original ignorado e registrado" ($r.Fim -and $r.Rc -eq 0 -and $r.Texto -match '--camera original ignorado' -and $r.Texto -match '\[camera\] modo livre') "rc=$($r.Rc)"

$r = Rodar $original "$headless --camera livre"
Check "E4 Original: a camera livre continua uma escolha" ($r.Fim -and $r.Rc -eq 0 -and $r.Texto -match '\[edicao\] perfil original' -and $r.Texto -match '\[camera\] modo livre') "rc=$($r.Rc)"

$r = Rodar $invalida $headless
Check "E5 perfil invalido: aviso e Original" ($r.Fim -and $r.Rc -eq 0 -and $r.Texto -match 'edicao\.perfil invalido' -and $r.Texto -match '\[edicao\] perfil original' -and $r.Texto -notmatch 'modo livre') "rc=$($r.Rc)"

# opcao recusada: a mensagem de uso fica aberta numa caixa, por isso o limite curto
$r = Rodar $original "$headless --edicao remastered" 8000
Check "E6 a linha de comando nao troca a edicao" ((-not $r.Fim -or $r.Rc -eq 2) -and $r.Texto -match '--camera original' -and $r.Texto -notmatch '\[edicao\] perfil') "rc=$($r.Rc)"

# E7: janela, com o controles.toml de um jogador da Original: a preferencia e ignorada, registrada e o arquivo nao muda
$ctl = Join-Path $remaster "controles.toml"
[IO.File]::WriteAllText($ctl, "# meu arquivo`n[camera]`nmodo = `"original`"`nmovimento = `"original`"`n")
$antes = (Get-FileHash -LiteralPath $ctl).Hash
$o = Join-Path $Saida "janela.txt"
$p = Start-Process -FilePath (Join-Path $remaster "BlueStinger.exe") -WorkingDirectory $remaster -PassThru -RedirectStandardError $o -RedirectStandardOutput "$o.out"
$texto = ""
for ($i = 0; $i -lt 60 -and $texto -notmatch 'modo livre'; $i++) {
    Start-Sleep -Milliseconds 500
    $texto = [string](Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue)
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
$p.WaitForExit(10000) | Out-Null
Check "E7 janela: controles.toml com Original ignorado e registrado" ($texto -match 'camera original ignorada' -and $texto -match 'movimento original ignorado' -and $texto -match 'modo livre')
Check "E7 janela: o arquivo do jogador nao mudou" ((Get-FileHash -LiteralPath $ctl).Hash -eq $antes)
Remove-Item -LiteralPath $ctl -Force

# E8: o menu real, pelo controle virtual (botoes como em tools/dev/n7_menu_pausa.py). Pagina Controles > Camera, Direita na
# primeira linha e sair. Na Remastered essa linha e a sensibilidade: ela muda no arquivo e modo e movimento nao. Na Original
# a mesma sequencia acerta o Modo, e o arquivo muda: e a prova de que o caso enxerga uma troca de modo
# N9.2: na Remastered as configuracoes abrem pelo menu inicial nativo (que aparece no "PRESS START", ~25 s): Configuracoes
# (Continuar e Carregar desativados sem perfil), a mesma pagina Controles > Camera, Direita, e volta ao menu inicial
function Menu($dir, $nome) {
    $ctl = Join-Path $dir "controles.toml"
    [IO.File]::WriteAllText($ctl, "[camera]`nmodo = `"original`"`nmovimento = `"original`"`nsensibilidade_x = 100`n")
    $linhas = @("0 conectar")
    if ($nome -eq "remastered") { $t = 32000; $seq = @("dpdown", "a", "dpdown", "dpdown", "dpdown", "a", "a", "dpright", "b", "b", "b") }
    else { $t = 6000; $seq = @("back", "dpdown", "dpdown", "dpdown", "a", "a", "dpright", "b", "b", "b") }
    foreach ($b in $seq) { $linhas += "$t $b 1", ("{0} {1} 0" -f ($t + 100), $b); $t += 500 }
    $linhas += "$($t + 1500) sair"
    $pad = Join-Path $Saida "pad-$nome.txt"; [IO.File]::WriteAllLines($pad, $linhas)
    $o = Join-Path $Saida "menu-$nome.txt"
    $env:BS_CONTROLE_VIRTUAL = $pad; $env:BS_RELOGIO_FIXO = "1700000000"
    # N9.2: um cartao vazio do teste (sem perfil, o menu inicial da Remastered comeca em Novo jogo; os saves do jogador nao entram)
    $cartao = Join-Path $Saida "saves-$nome"
    $p = Start-Process -FilePath (Join-Path $dir "BlueStinger.exe") -WorkingDirectory $dir -PassThru -RedirectStandardError $o -RedirectStandardOutput "$o.out" -ArgumentList "--saves-pasta `"$cartao`""
    Remove-Item Env:BS_CONTROLE_VIRTUAL, Env:BS_RELOGIO_FIXO
    $null = $p.Handle
    $fim = $p.WaitForExit(90000)
    if (-not $fim) { Stop-Process -Id $p.Id -Force }
    $r = [pscustomobject]@{ Fim = $fim; Rc = $p.ExitCode; Log = [string](Get-Content -LiteralPath $o -Raw -ErrorAction SilentlyContinue); Toml = [string](Get-Content -LiteralPath $ctl -Raw) }
    Remove-Item -LiteralPath $ctl -Force
    return $r
}
$r = Menu $remaster "remastered"
Check "E8 Remastered: o menu grava a sensibilidade e nao a camera imposta" ($r.Fim -and $r.Rc -eq 0 -and $r.Log -match 'preferencias gravadas' -and $r.Toml -match 'modo = "original"' -and $r.Toml -match 'movimento = "original"' -and $r.Toml -match 'sensibilidade_x = 110') "rc=$($r.Rc)"
$r = Menu $original "original"
Check "E8 Original: o mesmo roteiro troca o Modo no arquivo" ($r.Fim -and $r.Rc -eq 0 -and $r.Log -match 'preferencias gravadas' -and $r.Toml -match 'modo = "livre"' -and $r.Toml -match 'sensibilidade_x = 100') "rc=$($r.Rc)"

$falhas = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ("{0} ok, {1} falha(s)" -f ($results.Count - $falhas), $falhas)
exit $(if ($falhas) { 1 } else { 0 })
