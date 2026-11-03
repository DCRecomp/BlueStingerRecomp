# N9-L (L4): a decodificacao dos SFD do disco no Windows, para o teste sfd_disc do Linux comparar conteudo (BS_SFD_REF).
# Para cada .SFD: SHA-256 dos quadros do pl_mpeg em RGB24 (BS_PROBE_RAW) e do PCM do ADX (BS_PROBE_DUMP, .s16), pelo
# bs-video-probe compilado com o MSVC. Os arquivos temporarios ficam em %TEMP% e sao apagados; o TSV fica fora do repositorio.
#   powershell -File tools/dev/sfd_referencia_windows.ps1 -BsData game-data\BSDATA -Probe build\msvc-release\builder\bs-video-probe.exe -Saida <tsv>
param([Parameter(Mandatory = $true)][string]$BsData, [Parameter(Mandatory = $true)][string]$Probe, [Parameter(Mandatory = $true)][string]$Saida)
$ErrorActionPreference = "Stop"
$tmp = Join-Path $env:TEMP ("bs-sfd-ref-" + [guid]::NewGuid().ToString("N")); New-Item -ItemType Directory $tmp | Out-Null
try {
    $linhas = @("filme`trgb24_sha256`tpcm_sha256")
    foreach ($f in Get-ChildItem -LiteralPath $BsData -Filter *.SFD | Sort-Object Name) {
        $raw = Join-Path $tmp "quadros.rgb"
        $env:BS_PROBE_RAW = $raw; $env:BS_PROBE_DUMP = $tmp
        & $Probe $f.FullName | Out-Null
        $rc = $LASTEXITCODE
        Remove-Item Env:BS_PROBE_RAW, Env:BS_PROBE_DUMP
        if ($rc -ne 0) { throw "bs-video-probe falhou em $($f.Name) (rc=$rc)" }
        $s16 = Join-Path $tmp ($f.BaseName + ".s16")
        $linhas += "{0}`t{1}`t{2}" -f $f.Name, (Get-FileHash -LiteralPath $raw).Hash.ToLower(), (Get-FileHash -LiteralPath $s16).Hash.ToLower()
        Remove-Item -LiteralPath $raw, $s16, (Join-Path $tmp ($f.BaseName + ".adx"))
    }
    [IO.File]::WriteAllLines($Saida, $linhas)
    "{0} filmes -> {1}" -f ($linhas.Count - 1), $Saida
} finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
