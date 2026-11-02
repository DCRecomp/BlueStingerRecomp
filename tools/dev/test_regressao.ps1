# Teste sintetico do executor de regressao (tools/dev/regressao.ps1): repositorio git temporario com
# etapas falsas (BS_REGRESSAO_ETAPAS), sem builds nem jogo. Confere quando cada etapa e reaproveitada.
#   powershell -File tools/dev/test_regressao.ps1
$ErrorActionPreference = "Continue"
$src  = Join-Path $PSScriptRoot "regressao.ps1"
$root = Join-Path $env:TEMP ("bs_test_regressao_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$est  = Join-Path $root "estado"
New-Item -ItemType Directory -Force "$root\repo\tools\dev", "$root\repo\tools\dist", "$root\repo\src" | Out-Null
$repo = "$root\repo"
Copy-Item $src "$repo\tools\dev\regressao.ps1"
$marks = "$root\execucoes.txt"
# cada script falso anota que rodou; test_b falha se existir o arquivo "falhar"; test_a trava (dorme) se
# existir "travar", anotando o proprio PID (o executor tem que encerrar a arvore no limite de tempo)
foreach ($n in "make_x", "test_a", "test_b", "test_d") {
    $fail = if ($n -eq "test_b") { "if (Test-Path '$root\falhar') { exit 1 }" }
            elseif ($n -eq "test_a") { "if (Test-Path '$root\travar') { Set-Content '$root\pid.txt' `$PID; Write-Host 'dormindo'; Start-Sleep 600 }" } else { "" }
    "Add-Content '$marks' '$n'`n$fail`nexit 0" | Out-File "$repo\tools\dist\$n.ps1" -Encoding utf8
}
"codigo 1" | Out-File "$repo\src\code.txt" -Encoding utf8
"doc" | Out-File "$repo\LEIA.md" -Encoding utf8
@'
param($repo)
@(
    @{ N = "make_x"; T = "suite"; A = @("$repo\tools\dist\make_x.ps1"); Stop = $true },
    @{ N = "test_a"; T = "suite"; A = @("$repo\tools\dist\test_a.ps1") },
    @{ N = "test_b"; T = "suite"; A = @("$repo\tools\dist\test_b.ps1") }
)
'@ | Out-File "$root\etapas.ps1" -Encoding utf8
Push-Location $repo
git init -q; git -c user.email=t@t -c user.name=t add -A; git -c user.email=t@t -c user.name=t commit -qm base
Pop-Location
$env:BS_REGRESSAO_ETAPAS = "$root\etapas.ps1"

$fails = 0
$script:disco = "$root\disco1.bin"
"imagem 1" | Out-File "$root\disco1.bin" -Encoding ascii
"imagem 2" | Out-File "$root\disco2.bin" -Encoding ascii
function Run([string]$id, [string]$expect, [int]$expectRc = 0) {
    Remove-Item $marks -ErrorAction SilentlyContinue
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\dev\regressao.ps1" -Estado $est -Disc $script:disco *> (Join-Path $root "$($id.Split(' ')[0]).txt")
    $rc  = $LASTEXITCODE
    $ran = if (Test-Path $marks) { (Get-Content $marks) -join "," } else { "" }
    $ok  = $ran -eq $expect -and $rc -eq $expectRc
    Write-Host ("{0} {1}: rodaram [{2}] (esperado [{3}]), rc={4}" -f $(if ($ok) { "[ ok ]" } else { "[FALHA]" }), $id, $ran, $expect, $rc)
    if (-not $ok) { $script:fails++ }
}
Run "R1 primeira execucao"             "make_x,test_a,test_b"
Run "R2 nada mudou"                    ""
Add-Content "$repo\tools\dist\test_a.ps1" "# editado"
Run "R3 so o script de test_a mudou"   "test_a"
Add-Content "$repo\LEIA.md" "mais doc"
Run "R4 so documentacao"               ""
Add-Content "$repo\src\code.txt" "codigo 2"
Run "R5 codigo mudou: tudo"            "make_x,test_a,test_b"
Add-Content "$repo\tools\dist\make_x.ps1" "# produtor editado"
Run "R6 produtor mudou: tudo"          "make_x,test_a,test_b"
"novo" | Out-File "$repo\src\novo.txt" -Encoding utf8
Run "R7 arquivo nao versionado novo"   "make_x,test_a,test_b"
New-Item "$root\falhar" -ItemType File | Out-Null
Add-Content "$repo\tools\dist\test_b.ps1" "# b editado"
Run "R8 test_b falha"                  "test_b" 1
Run "R9 falha roda de novo"            "test_b" 1
Remove-Item "$root\falhar"
Run "R10 corrigida"                    "test_b"
Push-Location $repo; git -c user.email=t@t -c user.name=t add -A; git -c user.email=t@t -c user.name=t commit -qm c2; Pop-Location
Run "R11 commit (HEAD mudou): tudo"    "make_x,test_a,test_b"
# trava: um processo vivo com o PID da trava recusa a segunda execucao
$sleeper = Start-Process powershell -ArgumentList "-NoProfile", "-Command", "Start-Sleep 60" -PassThru -WindowStyle Hidden
"$($sleeper.Id)" | Out-File "$est\trava.txt" -Encoding ascii
Add-Content "$repo\tools\dist\test_a.ps1" "# de novo"
Run "R12 trava de outra regressao viva" "" 3
Stop-Process -Id $sleeper.Id -Force
Run "R13 trava de processo morto"      "test_a"
$lockLeft = Test-Path "$est\trava.txt"; $ativoLeft = Test-Path "$est\ativo.txt"
$dur = Select-String -Path "$est\regressao.log" -Pattern "duracoes" -Quiet
$ok = -not $lockLeft -and -not $ativoLeft -and $dur
Write-Host ("{0} R14 trava e ativo removidos no fim; duracoes no log" -f $(if ($ok) { "[ ok ]" } else { "[FALHA]" }))
if (-not $ok) { $fails++ }
# limite de tempo: a etapa travada vira rc=124, o processo some, a trava sai e a saida parcial fica no log
New-Item "$root\travar" -ItemType File | Out-Null
Add-Content "$repo\tools\dist\test_a.ps1" "# r15"
$env:BS_REGRESSAO_LIMITE_S = "5"
$t15 = Get-Date
Run "R15 etapa travada estoura o limite" "test_a" 1
$env:BS_REGRESSAO_LIMITE_S = ""
$hung = Get-Content "$root\pid.txt" -ErrorAction SilentlyContinue
$alive = $hung -and (Get-Process -Id $hung -ErrorAction SilentlyContinue)
$st15 = (Get-Content "$est\estado.tsv" | Select-String "^test_a\t124\t" -Quiet)
$out15 = Get-Content "$est\test_a.txt" -Raw
$ok = $hung -and -not $alive -and $st15 -and $out15 -match "dormindo" -and $out15 -match "TEMPO ESGOTADO" -and -not (Test-Path "$est\trava.txt") -and ((Get-Date) - $t15).TotalSeconds -lt 120
Write-Host ("{0} R16 processo travado encerrado (PID {1}), rc=124 no estado, saida parcial no log" -f $(if ($ok) { "[ ok ]" } else { "[FALHA]" }), $hung)
if (-not $ok) { $fails++ }
Remove-Item "$root\travar"
Run "R17 retomada refaz a etapa que estourou" "test_a"
# revisao Codex 20261028-011732: a definicao da etapa entra na chave. Duas etapas com o mesmo script e argumentos
# diferentes; mudar so o argumento de uma refaz so ela (o script anota "test_a" uma vez por execucao)
function Etapas([string]$casoA2) {
    @"
param(`$repo)
@(
    @{ N = "make_x"; T = "suite"; A = @("`$repo\tools\dist\make_x.ps1"); Stop = `$true },
    @{ N = "test_a"; T = "suite"; A = @("`$repo\tools\dist\test_a.ps1", "-Caso", "1") },
    @{ N = "test_a2"; T = "suite"; A = @("`$repo\tools\dist\test_a.ps1", "-Caso", "$casoA2") },
    @{ N = "test_b"; T = "suite"; A = @("`$repo\tools\dist\test_b.ps1") }
)
"@ | Out-File "$root\etapas.ps1" -Encoding utf8
}
Etapas "2"
Run "R18 argumentos novos e etapa nova com o mesmo script" "test_a,test_a"
Run "R19 nada mudou"                   ""
Etapas "3"
Run "R20 so o argumento de test_a2 mudou" "test_a"
# revisao Codex 20261028-012302: a identidade da midia (caminho e conteudo) entra na chave das etapas que a usam
@'
param($repo, $disc)
@(
    @{ N = "make_x"; T = "suite"; A = @("$repo\tools\dist\make_x.ps1"); Stop = $true },
    @{ N = "test_a"; T = "suite"; A = @("$repo\tools\dist\test_a.ps1") },
    @{ N = "test_d"; T = "suite"; A = @("$repo\tools\dist\test_d.ps1", "-Disc", $disc) }
)
'@ | Out-File "$root\etapas.ps1" -Encoding utf8
Run "R21 etapa com a midia"            "test_a,test_d"
Run "R22 nada mudou"                   ""
$script:disco = "$root\disco2.bin"
Run "R23 outro caminho de midia: so a etapa que a usa" "test_d"
"imagem 3" | Out-File "$root\disco2.bin" -Encoding ascii
Run "R24 mesmo caminho, conteudo novo" "test_d"
# esquema: booleano ou numero em texto e recusado (rc=2, nada roda)
foreach ($c in @(@("Stop", '"False"'), @("Max", '"0"'), @("Disc", '"1"'))) {
    @"
param(`$repo)
@( @{ N = "test_a"; T = "suite"; A = @("`$repo\tools\dist\test_a.ps1"); $($c[0]) = $($c[1]) } )
"@ | Out-File "$root\etapas.ps1" -Encoding utf8
    Run "R25 $($c[0]) textual recusado" "" 2
}
# N8.8: identidade funcional. Artefatos: um PE real (copia de um exe do Windows), uma DLL falsa (nao PE: entra crua) e um
# arquivo de dados. A etapa test_f tem Id; test_a nao (presa ao commit).
$art = "$root\artefatos"; New-Item -ItemType Directory -Force $art | Out-Null
Copy-Item "$env:SystemRoot\System32\whoami.exe" "$art\jogo.exe"
"dll 1" | Out-File "$art\lib.dll" -Encoding ascii
"dados 1" | Out-File "$art\dados.bin" -Encoding ascii
"Add-Content '$marks' 'test_f'`nexit 0" | Out-File "$repo\tools\dist\test_f.ps1" -Encoding utf8
@"
param(`$repo)
@(
    @{ N = "make_x"; T = "suite"; A = @("`$repo\tools\dist\make_x.ps1"); Stop = `$true },
    @{ N = "test_a"; T = "suite"; A = @("`$repo\tools\dist\test_a.ps1") },
    @{ N = "test_f"; T = "suite"; A = @("`$repo\tools\dist\test_f.ps1"); Id = @("$art") }
)
"@ | Out-File "$root\etapas.ps1" -Encoding utf8
function Commit($m) { Push-Location $repo; git -c user.email=t@t -c user.name=t add -A; git -c user.email=t@t -c user.name=t commit -qm $m; Pop-Location }
function PatchPE([int]$off, [byte]$v) { $b = [IO.File]::ReadAllBytes("$art\jogo.exe"); $b[$off] = $v; [IO.File]::WriteAllBytes("$art\jogo.exe", $b) }
$peOff = [BitConverter]::ToInt32([IO.File]::ReadAllBytes("$art\jogo.exe"), 0x3C)
# sem o calculador da identidade (exe_identidade.py ausente no repositorio): invalida, nada e reaproveitado
Commit "f0"
Run "R26 sem a ferramenta de identidade: tudo roda" "make_x,test_a,test_f"
Add-Content "$repo\src\code.txt" "f1"; Commit "f1"
Run "R27 commit novo, identidade indisponivel: tudo roda" "make_x,test_a,test_f"
Copy-Item (Join-Path $PSScriptRoot "exe_identidade.py") "$repo\tools\dev\exe_identidade.py"; Commit "f2"
Run "R28 com a ferramenta (tools mudou): tudo roda" "make_x,test_a,test_f"
Add-Content "$repo\src\code.txt" "f3"; Commit "f3"
Run "R29 commit novo, artefatos iguais: test_f reaproveitada" "make_x,test_a"
$st29 = Get-Content "$est\estado.tsv" | Select-String "^test_f\t0\t0\t.*identidade funcional" -Quiet
Write-Host ("{0} R30 o estado registra o reaproveitamento por identidade" -f $(if ($st29) { "[ ok ]" } else { "[FALHA]" })); if (-not $st29) { $fails++ }
PatchPE ($peOff + 8) 0x5A; Add-Content "$repo\src\code.txt" "f4"; Commit "f4"
Run "R31 so o TimeDateStamp do PE mudou: reaproveitada" "make_x,test_a"
$b = [IO.File]::ReadAllBytes("$art\jogo.exe"); $sec = $peOff + 24 + [BitConverter]::ToUInt16($b, $peOff + 20); $text = [BitConverter]::ToInt32($b, $sec + 20) + 0x40
PatchPE $text ($b[$text] -bxor 0xFF); Add-Content "$repo\src\code.txt" "f5"; Commit "f5"
Run "R32 um byte do codigo do exe mudou: roda" "make_x,test_a,test_f"
"dll 2" | Out-File "$art\lib.dll" -Encoding ascii; Add-Content "$repo\src\code.txt" "f6"; Commit "f6"
Run "R33 a DLL mudou: roda" "make_x,test_a,test_f"
"dados 2" | Out-File "$art\dados.bin" -Encoding ascii; Add-Content "$repo\src\code.txt" "f7"; Commit "f7"
Run "R34 um dado mudou: roda" "make_x,test_a,test_f"
"novo" | Out-File "$art\extra.txt" -Encoding ascii; Add-Content "$repo\src\code.txt" "f8"; Commit "f8"
Run "R35 arquivo novo nos artefatos: roda" "make_x,test_a,test_f"
$env:BS_REGRESSAO_TESTE_AMB = "1"; Add-Content "$repo\src\code.txt" "f9"; Commit "f9"
Run "R36 variavel BS_* nova no ambiente: roda" "make_x,test_a,test_f"
Remove-Item Env:\BS_REGRESSAO_TESTE_AMB; Add-Content "$repo\src\code.txt" "f10"; Commit "f10"
Run "R37 variavel removida: so a aprovacao mais recente (com a variavel) e candidata: roda" "make_x,test_a,test_f"
"config 1" | Out-File "$repo\tools\dist\roteiro_x.txt" -Encoding utf8; Commit "f11"
Run "R38 arquivo novo em tools (roteiro): roda" "make_x,test_a,test_f"
Add-Content "$repo\tools\dist\test_f.ps1" "# f editado"
Run "R39 so o script de test_f mudou: so ela" "test_f"
Add-Content "$repo\src\code.txt" "f12"; Commit "f12"
Run "R40 commit novo depois: reaproveitada de novo" "make_x,test_a"
New-Item "$root\falhar_f" -ItemType File | Out-Null
"if (Test-Path '$root\falhar_f') { Add-Content '$marks' 'test_f'; exit 1 }`nAdd-Content '$marks' 'test_f'`nexit 0" | Out-File "$repo\tools\dist\test_f.ps1" -Encoding utf8
Run "R41 test_f falha" "test_f" 1
Add-Content "$repo\src\code.txt" "f13"; Commit "f13"
Run "R42 uma falha nunca e reaproveitada" "make_x,test_a,test_f" 1
Remove-Item "$root\falhar_f"
Run "R43 corrigida" "test_f"
Remove-Item $marks -ErrorAction SilentlyContinue
& powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\dev\regressao.ps1" -Estado $est -Disc $script:disco -Desde test_f *> "$root\R44.txt"
$r44 = (Get-Content $marks -ErrorAction SilentlyContinue) -join ","
Write-Host ("{0} R44 -Desde refaz mesmo com a identidade igual: [{1}]" -f $(if ($r44 -eq "test_f") { "[ ok ]" } else { "[FALHA]" }), $r44); if ($r44 -ne "test_f") { $fails++ }
Remove-Item $marks -ErrorAction SilentlyContinue
& powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\dev\regressao.ps1" -Estado $est -Disc $script:disco -Novo *> "$root\R45.txt"
$r45 = (Get-Content $marks -ErrorAction SilentlyContinue) -join ","
Write-Host ("{0} R45 -Novo refaz tudo: [{1}]" -f $(if ($r45 -eq "make_x,test_a,test_f") { "[ ok ]" } else { "[FALHA]" }), $r45); if ($r45 -ne "make_x,test_a,test_f") { $fails++ }
# esquema: Id vazio ou com nao-texto e recusado
foreach ($c in @('@()', '@(1)')) {
    @"
param(`$repo)
@( @{ N = "test_f"; T = "suite"; A = @("`$repo\tools\dist\test_f.ps1"); Id = $c } )
"@ | Out-File "$root\etapas.ps1" -Encoding utf8
    Run "R46 Id invalido $c recusado" "" 2
}
Remove-Item Env:\BS_REGRESSAO_ETAPAS
if ($fails -eq 0) { Remove-Item -Recurse -Force $root } else { Write-Host "artefatos em $root" }
Write-Host "falhas: $fails"
exit $fails
