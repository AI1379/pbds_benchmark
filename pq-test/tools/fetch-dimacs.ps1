<#
.SYNOPSIS
    Download, verify and extract the official 9th DIMACS Implementation Challenge
    USA road-network data (USA-road-d / USA-road-t families) used by ads-projects/proj2.

.DESCRIPTION
    The official Challenge server (http://www.diag.uniroma1.it/challenge9/) has been
    offline since ~2023 (HTTP 502 / TLS failure).  This script restores the dataset
    from Internet Archive Wayback Machine captures and from a byte-identical GitHub
    mirror, then re-verifies every byte:

      1. download the original .gz from the first source whose SHA-256 matches the
         (hard-coded, previously verified) expected hash;
      2. check the raw file size and SHA-256;
      3. fully gunzip it (gzip CRC is checked by .NET GZipStream) to
         data/dimacs/<name> and check the extracted SHA-256;
      4. parse the extracted DIMACS text and cross-check the header counts:
            .gr : "p sp <n> <m>", number of "a" lines == m,
                  all node ids in [1,n], max node id == n, min/max arc weight
                  (also reports nodes that never appear in any arc);
            .co : "p aux sp co <n>", number of "v" lines == n,
                  all node ids in [1,n], max node id == n;
      5. write data/dimacs/MANIFEST.sha256 (sha256sum-compatible, paths relative to
         data/dimacs) covering every raw/extracted dataset file, the official sample
         files and the generated query files.

    Files are only re-downloaded when missing or when their hash does not match, so
    the script is safe to re-run.  It never touches source code; it only writes under
    <proj2>/data/dimacs/.

    Requirements: PowerShell 7+, curl.exe (bundled with Windows 10/11), tar.exe is
    NOT needed.  Optional: python (not required by this script).

.PARAMETER Set
    Which group of the hard-coded catalogue to process:
      All     = P0 + P1 + P2 (default: the 6 files this project actually needs)
      P0      = NY distance graph + NY coordinates (minimum viable data set)
      P1      = size ladder BAY / NE / E
      P2      = optional NY travel-time graph
      Samples = the official sample .gr/.co/.ss/.p2p files only (tiny, ~9 files)

.PARAMETER Files
    Explicit catalogue ids (or raw/plain file names), e.g.:
        -Files NY.gr,NY.co
        -Files USA-road-d.NY.gr.gz
    Overrides -Set.

.PARAMETER IncludeSamples
    Add the official sample files (data/dimacs/samples/) to the default selection.
    Use "-Set All -IncludeSamples" to restore absolutely everything.

.PARAMETER Root
    proj2 root directory.  Defaults to two levels above this script, i.e. running
    "pwsh pq-test/tools/fetch-dimacs.ps1" from anywhere restores into the repo.

.PARAMETER Force
    Re-download and re-extract even if the local files already verify.

.PARAMETER Offline
    Verify/extract only from files already on disk; never use the network.

.PARAMETER NoManifest
    Do not rewrite data/dimacs/MANIFEST.sha256.

.EXAMPLE
    pwsh pq-test/tools/fetch-dimacs.ps1
    Restore + verify the default P0+P1+P2 data set (~85 MB download, ~265 MB on disk).

.EXAMPLE
    pwsh pq-test/tools/fetch-dimacs.ps1 -Set P0
    Restore only NY.gr + NY.co (fast; enough for parser development).

.EXAMPLE
    pwsh pq-test/tools/fetch-dimacs.ps1 -Offline
    Verify the local data files without network access.

.EXAMPLE
    pwsh pq-test/tools/fetch-dimacs.ps1 -Files NY.gr,NY.co -Force
    Force re-download of the two P0 files.

.NOTES
    Sources (verified 2026-10-26):
      * Wayback: https://web.archive.org/web/<timestamp>id_/<official-url>
        (the trailing "id_" returns the original bytes, no Wayback rewriting)
      * GitHub mirror (only for USA-road-d.NY.co.gz, which never was archived):
        https://github.com/XiangQibu/NEWYORK_DATASET
        Its USA-road-d.NY.gr.gz is SHA-256-identical to the Wayback official copy,
        which is why the mirror is trusted for the .co file.
    CDX "length" values are WARC record sizes and may differ slightly from the payload
    size; this script always checks the real byte count + SHA-256.
#>
[CmdletBinding()]
param(
    [ValidateSet('All', 'P0', 'P1', 'P2', 'Samples')]
    [string]$Set = 'All',

    [string[]]$Files,

    [switch]$IncludeSamples,

    [string]$Root,

    [switch]$Force,

    [switch]$Offline,

    [switch]$NoManifest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Hard-coded catalogue.  Sizes/SHA-256 were established once by downloading from
# the official server before it died / from Wayback and cross-checking every
# record (see docs/DATA.md for the full verification record).
# ---------------------------------------------------------------------------
$Catalog = @(
    [pscustomobject]@{
        Id               = 'NY.gr'
        Priority         = 'P0'
        Kind             = 'gr'
        RawName          = 'USA-road-d.NY.gr.gz'
        PlainName        = 'USA-road-d.NY.gr'
        Bytes            = 3667907
        Sha256           = '7b2446c7ffe6179efbc42af6812448e8cb782f12968ca2bb1d50d979343056d4'
        PlainBytes       = 14437459
        PlainSha256      = '37f5b3eff82330850466956b6c036a8ab8551512bdc7509edca1d4c6cf943fa3'
        Nodes            = 264346
        Arcs             = 733846
        WeightMin        = 1
        WeightMax        = 36946
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NY.gr.gz'
        WaybackTimestamp = '20230413134046'
        Urls             = @(
            'https://web.archive.org/web/20230413134046id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NY.gr.gz',
            'https://raw.githubusercontent.com/XiangQibu/NEWYORK_DATASET/main/USA-road-d.NY.gr.gz'
        )
        Source           = 'Wayback Machine official archive (fallback: GitHub mirror, byte-identical)'
    },
    [pscustomobject]@{
        Id               = 'NY.co'
        Priority         = 'P0'
        Kind             = 'co'
        RawName          = 'USA-road-d.NY.co.gz'
        PlainName        = 'USA-road-d.NY.co'
        Bytes            = 2058598
        Sha256           = '02547a628742164e02bd73f59e33a65d23dfac8fc54c09275c8b9169c38eafdf'
        PlainBytes       = 7290781
        PlainSha256      = 'c39ec9b87c66d6e254f80cd3cfe20c7eb3c5ce4aca4063dc2efc228ab12fa95f'
        Nodes            = 264346
        Arcs             = $null
        WeightMin        = $null
        WeightMax        = $null
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NY.co.gz'
        WaybackTimestamp = $null   # official NY.co.gz was never captured by the Wayback Machine
        Urls             = @(
            'https://raw.githubusercontent.com/XiangQibu/NEWYORK_DATASET/main/USA-road-d.NY.co.gz'
        )
        Source           = 'GitHub mirror (official .co never archived by Wayback; NY.gr.gz in the same repo is byte-identical)'
    },
    [pscustomobject]@{
        Id               = 'BAY.gr'
        Priority         = 'P1'
        Kind             = 'gr'
        RawName          = 'USA-road-d.BAY.gr.gz'
        PlainName        = 'USA-road-d.BAY.gr'
        Bytes            = 4130672
        Sha256           = '630c4a96869b3ecdd631ceb0f63923fb954ee4c7c26d983863612e2c4442462e'
        PlainBytes       = 15851432
        PlainSha256      = '75a1a78d1f1bd1d8d86b2dc81b0c2ac30f701e078c514c9f9b953130b83f979e'
        Nodes            = 321270
        Arcs             = 800172
        WeightMin        = 1
        WeightMax        = 94305
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.BAY.gr.gz'
        WaybackTimestamp = '20240122163700'
        Urls             = @(
            'https://web.archive.org/web/20240122163700id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.BAY.gr.gz'
        )
        Source           = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id               = 'NE.gr'
        Priority         = 'P1'
        Kind             = 'gr'
        RawName          = 'USA-road-d.NE.gr.gz'
        PlainName        = 'USA-road-d.NE.gr'
        Bytes            = 21755467
        Sha256           = 'edd4f51db01345f44d8972a8c372b6baaa5b2026182dd95668572035db55471e'
        PlainBytes       = 82240991
        PlainSha256      = '24985e9b4d7feeb00e2e52394627bb00ea52e7f85b2f7640aa6139fd8adc0394'
        Nodes            = 1524453
        Arcs             = 3897636
        WeightMin        = 1
        WeightMax        = 63247
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NE.gr.gz'
        WaybackTimestamp = '20240123141930'
        Urls             = @(
            'https://web.archive.org/web/20240123141930id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NE.gr.gz'
        )
        Source           = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id               = 'E.gr'
        Priority         = 'P1'
        Kind             = 'gr'
        RawName          = 'USA-road-d.E.gr.gz'
        PlainName        = 'USA-road-d.E.gr'
        Bytes            = 51710551
        Sha256           = 'd2e59a82ec5892491d4bfe1e30b0fd3344ae5864e783eb225a843bc5368fe17d'
        PlainBytes       = 193023156
        PlainSha256      = '80a6b825405501b65878d84c92a0ce54c27ec0e91f6c6724e28663fb114f0509'
        Nodes            = 3598623
        Arcs             = 8778114
        WeightMin        = 1
        WeightMax        = 200760
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.E.gr.gz'
        WaybackTimestamp = '20231202184547'
        Urls             = @(
            'https://web.archive.org/web/20231202184547id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.E.gr.gz'
        )
        Source           = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id               = 'NY.t.gr'
        Priority         = 'P2'
        Kind             = 'gr'
        RawName          = 'USA-road-t.NY.gr.gz'
        PlainName        = 'USA-road-t.NY.gr'
        Bytes            = 3767743
        Sha256           = 'de902573142dbf12c42b065d9a6a679e4794372eb91d2748ee78089720377156'
        PlainBytes       = 14759295
        PlainSha256      = '948377c7f0a974bdb0a9be76fd5b7ceff95bb78c2a011fa6661bde2f2f06f732'
        Nodes            = 264346
        Arcs             = 733846
        WeightMin        = 2
        WeightMax        = 92366
        OfficialUrl      = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-t/USA-road-t.NY.gr.gz'
        WaybackTimestamp = '20240123141344'
        Urls             = @(
            'https://web.archive.org/web/20240123141344id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-t/USA-road-t.NY.gr.gz',
            'https://raw.githubusercontent.com/XiangQibu/NEWYORK_DATASET/main/USA-road-t.NY.gr.gz'
        )
        Source           = 'Wayback Machine official archive (fallback: GitHub mirror, byte-identical)'
    },
    # ---- official sample problem files (samples/ directory of the challenge) ----
    [pscustomobject]@{
        Id = 'sample.gr'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.gr'; PlainName = 'sample.gr'
        Bytes = 305; Sha256 = '2a0dc45b7dfed3ff26d7d1a084529d4eafed422baf5d0381d22741e010d193b2'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.gr'
        WaybackTimestamp = '20171001221232'
        Urls = @('https://web.archive.org/web/20171001221232id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.gr')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.co'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.co'; PlainName = 'sample.co'
        Bytes = 279; Sha256 = '13c0eff2673b55eee272fdb97d0f811b7411b9bae5b659a4ce043f860064fc54'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.co'
        WaybackTimestamp = '20170930085529'
        Urls = @('https://web.archive.org/web/20170930085529id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.co')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.ss'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.ss'; PlainName = 'sample.ss'
        Bytes = 201; Sha256 = 'e5551105c6cd2376a7157daecb19f7ef58bf9933ba7fe7c249c5ff58b02db9fa'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.ss'
        WaybackTimestamp = '20171001221247'
        Urls = @('https://web.archive.org/web/20171001221247id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.ss')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.ss.chk'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.ss.chk'; PlainName = 'sample.ss.chk'
        Bytes = 923; Sha256 = '9e1ecc5db2d8943fb95788844391da634b4ccb6c7a01014711c07aaebce0fa01'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.ss.chk'
        WaybackTimestamp = '20171001221252'
        Urls = @('https://web.archive.org/web/20171001221252id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.ss.chk')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.ss.res'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.ss.res'; PlainName = 'sample.ss.res'
        Bytes = 1805; Sha256 = 'a59b4e3acd6a246dc8a71ab54f5115774587bab1a60191a8c22c3ae558d6f0f7'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.ss.res'
        WaybackTimestamp = '20171001221257'
        Urls = @('https://web.archive.org/web/20171001221257id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.ss.res')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.p2p'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.p2p'; PlainName = 'sample.p2p'
        Bytes = 209; Sha256 = '67b90c690f429c83e4f97c6e26ffe1084e78e1fcb5d64d400afd954c4b64cac2'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.p2p'
        WaybackTimestamp = '20171001221237'
        Urls = @('https://web.archive.org/web/20171001221237id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.p2p')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.p2p.chk'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.p2p.chk'; PlainName = 'sample.p2p.chk'
        Bytes = 863; Sha256 = 'fd202c5220848889b45950230c3d644d5f35139e09032e592197b2fb35650b61'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.p2p.chk'
        WaybackTimestamp = '20171001221525'
        Urls = @('https://web.archive.org/web/20171001221525id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.p2p.chk')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.p2p.p.res'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.p2p.p.res'; PlainName = 'sample.p2p.p.res'
        Bytes = 1322; Sha256 = 'e216497b4287899ab24fa1c197c6f52ef76a083c437cf1be4d2a7ea68af72f39'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.p2p.p.res'
        WaybackTimestamp = '20171001221530'
        Urls = @('https://web.archive.org/web/20171001221530id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.p2p.p.res')
        Source = 'Wayback Machine official archive'
    },
    [pscustomobject]@{
        Id = 'sample.p2p.q.res'; Priority = 'S'; Kind = 'sample'; RawName = 'sample.p2p.q.res'; PlainName = 'sample.p2p.q.res'
        Bytes = 1895; Sha256 = 'cbed7d88599f230c3e6cb2d2ad6c3ecf008ff91e6e09a540af987663e6c997ff'
        OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/samples/sample.p2p.q.res'
        WaybackTimestamp = '20171001221242'
        Urls = @('https://web.archive.org/web/20171001221242id_/http://www.diag.uniroma1.it:80/challenge9/samples/sample.p2p.q.res')
        Source = 'Wayback Machine official archive'
    }
)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
if (-not $Root) {
    $Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
if (-not (Test-Path -LiteralPath (Join-Path $Root 'CMakeLists.txt'))) {
    throw "Not a proj2 root directory: '$Root' (CMakeLists.txt not found). Use -Root to specify the proj2 directory."
}
$Root      = (Resolve-Path -LiteralPath $Root).Path
$DataDir   = Join-Path $Root 'data/dimacs'
$RawDir    = Join-Path $DataDir 'raw'
$SampleDir = Join-Path $DataDir 'samples'
$QueryDir  = Join-Path $DataDir 'queries'
$Manifest  = Join-Path $DataDir 'MANIFEST.sha256'

New-Item -ItemType Directory -Force -Path $RawDir, $SampleDir, $QueryDir | Out-Null

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Banner([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Format-Bytes([long]$Bytes) {
    if ($Bytes -ge 1048576) { return ('{0:N1} MiB' -f ($Bytes / 1048576.0)) }
    if ($Bytes -ge 1024) { return ('{0:N1} KiB' -f ($Bytes / 1024.0)) }
    return "$Bytes B"
}

function Select-Entries {
    if ($Files -and $Files.Count -gt 0) {
        $picked = [System.Collections.Generic.List[object]]::new()
        foreach ($want in $Files) {
            $hit = $null
            foreach ($e in $Catalog) {
                if ($want -ieq $e.Id -or $want -ieq $e.RawName -or $want -ieq $e.PlainName) { $hit = $e; break }
            }
            if (-not $hit) {
                $ids = ($Catalog | ForEach-Object { $_.Id }) -join ', '
                throw "Unknown file '$want'. Available ids: $ids"
            }
            if (-not ($picked | Where-Object { $_.Id -eq $hit.Id })) { $picked.Add($hit) }
        }
        return $picked.ToArray()
    }
    $prios = switch ($Set) {
        'P0' { @('P0') }
        'P1' { @('P1') }
        'P2' { @('P2') }
        'Samples' { @('S') }
        default { @('P0', 'P1', 'P2') }
    }
    $sel = @($Catalog | Where-Object { $prios -contains $_.Priority })
    if ($IncludeSamples -and $Set -ne 'Samples') {
        $sel = @($sel) + @($Catalog | Where-Object { $_.Priority -eq 'S' })
    }
    return $sel
}

function Download-File {
    param([pscustomobject]$Entry)

    $dest = if ($Entry.Kind -eq 'sample') { Join-Path $SampleDir $Entry.RawName } else { Join-Path $RawDir $Entry.RawName }

    if (-not $Force -and (Test-Path -LiteralPath $dest)) {
        if ((Get-Item -LiteralPath $dest).Length -eq $Entry.Bytes -and (Get-Sha256 $dest) -eq $Entry.Sha256) {
            Write-Host ("  [cached]  {0}  ({1}, sha256 OK)" -f $Entry.RawName, (Format-Bytes $Entry.Bytes)) -ForegroundColor DarkGray
            return $dest
        }
        Write-Host ("  [stale]   {0} local file size/hash mismatch, re-downloading" -f $Entry.RawName) -ForegroundColor Yellow
    }

    if ($Offline) {
        throw "Offline mode: missing or corrupted file: $dest"
    }

    $part = "$dest.part"
    $errors = [System.Collections.Generic.List[string]]::new()
    foreach ($url in $Entry.Urls) {
        Write-Host ("  [fetch]   {0}" -f $url) -ForegroundColor DarkGray
        if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
        $curlArgs = @(
            '-L', '--ssl-no-revoke', '--fail',
            '--retry', '6', '--retry-delay', '5', '--retry-all-errors',
            '--connect-timeout', '30', '--max-time', '900',
            '-o', $part, '-w', 'http=%{http_code} bytes=%{size_download} secs=%{time_total}'
        )
        $msg = & curl.exe @curlArgs $url 2>$null
        $curlExit = $LASTEXITCODE
        Write-Host ("            curl exit={0}  {1}" -f $curlExit, $msg) -ForegroundColor DarkGray

        if ($curlExit -ne 0 -or -not (Test-Path -LiteralPath $part)) {
            $errors.Add("$url : curl exit $curlExit")
            continue
        }
        $len = (Get-Item -LiteralPath $part).Length
        if ($len -ne $Entry.Bytes) {
            $errors.Add("$url : size $len != expected $($Entry.Bytes)")
            continue
        }
        $hash = Get-Sha256 $part
        if ($hash -ne $Entry.Sha256) {
            $errors.Add("$url : sha256 $hash != expected $($Entry.Sha256)")
            continue
        }
        Move-Item -LiteralPath $part -Destination $dest -Force
        Write-Host ("  [ok]      {0}  {1}  sha256={2}" -f $Entry.RawName, (Format-Bytes $len), $hash) -ForegroundColor Green
        return $dest
    }
    if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
    throw ("Download failed: {0}`n  {1}" -f $Entry.RawName, ($errors -join "`n  "))
}

function Expand-Dataset {
    param([pscustomobject]$Entry, [string]$RawPath)

    $plain = Join-Path $DataDir $Entry.PlainName
    if (-not $Force -and (Test-Path -LiteralPath $plain)) {
        if ((Get-Item -LiteralPath $plain).Length -eq $Entry.PlainBytes -and (Get-Sha256 $plain) -eq $Entry.PlainSha256) {
            Write-Host ("  [cached]  {0}  ({1}, extracted sha256 OK)" -f $Entry.PlainName, (Format-Bytes $Entry.PlainBytes)) -ForegroundColor DarkGray
            return $plain
        }
        Write-Host ("  [stale]   {0} extracted file size/hash mismatch, re-extracting" -f $Entry.PlainName) -ForegroundColor Yellow
    }

    $part = "$plain.part"
    Write-Host ("  [gunzip]  {0} -> {1}" -f $Entry.RawName, $Entry.PlainName) -ForegroundColor DarkGray
    if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
    $inStream = [System.IO.File]::OpenRead($RawPath)
    try {
        $gz = [System.IO.Compression.GZipStream]::new($inStream, [System.IO.Compression.CompressionMode]::Decompress)
        try {
            $outStream = [System.IO.File]::Create($part)
            try {
                $gz.CopyTo($outStream, 1048576)   # gzip CRC is validated at end-of-stream
            }
            finally { $outStream.Dispose() }
        }
        finally { $gz.Dispose() }
    }
    finally { $inStream.Dispose() }

    $len = (Get-Item -LiteralPath $part).Length
    if ($len -ne $Entry.PlainBytes) { Remove-Item -LiteralPath $part -Force; throw "Extracted size $len != expected $($Entry.PlainBytes): $($Entry.PlainName)" }
    $hash = Get-Sha256 $part
    if ($hash -ne $Entry.PlainSha256) { Remove-Item -LiteralPath $part -Force; throw "Extracted sha256 mismatch: $($Entry.PlainName) -> $hash" }
    Move-Item -LiteralPath $part -Destination $plain -Force
    Write-Host ("  [ok]      {0}  {1}  sha256={2}" -f $Entry.PlainName, (Format-Bytes $len), $hash) -ForegroundColor Green
    return $plain
}

function Test-DimacsGraph {
    param([string]$Path, [pscustomobject]$Entry)

    $errors = [System.Collections.Generic.List[string]]::new()
    $headerLine = $null
    $n = $null
    $m = $null
    $arcLines = 0
    $malformed = 0
    $outOfRange = 0
    $minId = [long]::MaxValue
    $maxId = 0
    $wMin = [long]::MaxValue
    $wMax = [long]::MinValue
    $present = $null
    $lineNo = 0
    $weightSum = [long]0

    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::ASCII, $false, 1048576)
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            $lineNo++
            $len = $line.Length
            if ($len -lt 2) { continue }
            $ch = $line[0]
            if ($ch -eq [char]'c') { continue }
            if ($ch -eq [char]'p') {
                if ($null -ne $headerLine) { $errors.Add("line ${lineNo}: duplicate 'p' line"); continue }
                $headerLine = $line
                $tok = $line.Split(' ')
                if ($tok.Count -ne 4 -or $tok[1] -ne 'sp') {
                    $errors.Add("line ${lineNo}: expected 'p sp <n> <m>', got: $line")
                    continue
                }
                $n = [int]$tok[2]
                $m = [int]$tok[3]
                $present = [byte[]]::new($n + 1)
                continue
            }
            if ($ch -eq [char]'a') {
                $arcLines++
                $tok = $line.Split(' ')
                if ($tok.Count -ne 4) {
                    $malformed++
                    if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: 'a' line field count != 4: $line") }
                    continue
                }
                $u = 0; $v = 0; $w = 0L
                if (-not [int]::TryParse($tok[1], [ref]$u) -or -not [int]::TryParse($tok[2], [ref]$v) -or
                    -not [long]::TryParse($tok[3], [ref]$w)) {
                    $malformed++
                    if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: non-integer arc field: $line") }
                    continue
                }
                if ($w -lt $wMin) { $wMin = $w }
                if ($w -gt $wMax) { $wMax = $w }
                $weightSum += $w
                if ($u -lt $minId) { $minId = $u }
                if ($v -lt $minId) { $minId = $v }
                if ($u -gt $maxId) { $maxId = $u }
                if ($v -gt $maxId) { $maxId = $v }
                if ($null -eq $n) { $errors.Add("line ${lineNo}: arc line appears before the 'p' line"); continue }
                if (1 -le $u -and $u -le $n) { if ($present[$u] -eq 0) { $present[$u] = 1 } }
                else { $outOfRange++; if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: head $u out of range [1,$n]") } }
                if (1 -le $v -and $v -le $n) { if ($present[$v] -eq 0) { $present[$v] = 1 } }
                else { $outOfRange++; if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: tail $v out of range [1,$n]") } }
                continue
            }
            $malformed++
            if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: unknown record type '$ch': $line") }
        }
    }
    finally { $reader.Dispose() }

    if ($null -eq $headerLine) { $errors.Add("missing 'p sp <n> <m>' problem line") }
    if ($null -ne $n -and $null -ne $m) {
        if ($arcLines -ne $m) { $errors.Add("arc line count $arcLines != header m=$m") }
        if ($null -ne $Entry -and $m -ne $Entry.Arcs) { $errors.Add("header m=$m != expected $($Entry.Arcs)") }
        if ($null -ne $Entry -and $n -ne $Entry.Nodes) { $errors.Add("header n=$n != expected $($Entry.Nodes)") }
    }
    if ($maxId -ne $n) { $errors.Add("max node id $maxId != n $n") }
    if ($outOfRange -gt 0) { $errors.Add("$outOfRange node ids out of range") }
    if ($malformed -gt 0) { $errors.Add("$malformed malformed lines") }
    if ($arcLines -eq 0) { $errors.Add("no arc lines found") }
    if ($null -ne $Entry -and $null -ne $Entry.WeightMin -and $wMin -ne $Entry.WeightMin) { $errors.Add("weight min $wMin != expected $($Entry.WeightMin)") }
    if ($null -ne $Entry -and $null -ne $Entry.WeightMax -and $wMax -ne $Entry.WeightMax) { $errors.Add("weight max $wMax != expected $($Entry.WeightMax)") }

    $missing = 0
    if ($null -ne $present -and $null -ne $n -and $n -le 8000000) {
        for ($i = 1; $i -le $n; $i++) { if ($present[$i] -eq 0) { $missing++ } }
    }

    return [pscustomobject]@{
        Ok        = ($errors.Count -eq 0)
        Errors    = $errors
        Nodes     = $n
        Arcs      = $m
        ArcLines  = $arcLines
        MinId     = $(if ($minId -eq [long]::MaxValue) { $null } else { $minId })
        MaxId     = $maxId
        WeightMin = $(if ($wMin -eq [long]::MaxValue) { $null } else { $wMin })
        WeightMax = $(if ($wMax -eq [long]::MinValue) { $null } else { $wMax })
        WeightSum = $weightSum
        Missing   = $missing
        Lines     = $lineNo
    }
}

function Test-DimacsCoords {
    param([string]$Path, [pscustomobject]$Entry)

    $errors = [System.Collections.Generic.List[string]]::new()
    $headerLine = $null
    $n = $null
    $vLines = 0
    $malformed = 0
    $outOfRange = 0
    $duplicates = 0
    $maxId = 0
    $minLon = [double]::MaxValue; $maxLon = [double]::MinValue
    $minLat = [double]::MaxValue; $maxLat = [double]::MinValue
    $present = $null
    $lineNo = 0
    $inv = [System.Globalization.CultureInfo]::InvariantCulture

    $reader = [System.IO.StreamReader]::new($Path, [System.Text.Encoding]::ASCII, $false, 1048576)
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            $lineNo++
            if ($line.Length -lt 2) { continue }
            $ch = $line[0]
            if ($ch -eq [char]'c') { continue }
            if ($ch -eq [char]'p') {
                $headerLine = $line
                $tok = $line.Split(' ')
                if ($tok.Count -ne 5 -or $tok[1] -ne 'aux' -or $tok[3] -ne 'co') {
                    $errors.Add("line ${lineNo}: expected 'p aux sp co <n>', got: $line")
                    continue
                }
                $n = [int]$tok[4]
                $present = [byte[]]::new($n + 1)
                continue
            }
            if ($ch -eq [char]'v') {
                $vLines++
                $tok = $line.Split(' ')
                if ($tok.Count -ne 4) {
                    $malformed++
                    if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: 'v' line field count != 4: $line") }
                    continue
                }
                $id = 0; $lon = 0.0; $lat = 0.0
                if (-not [int]::TryParse($tok[1], [ref]$id) -or
                    -not [double]::TryParse($tok[2], [System.Globalization.NumberStyles]::Float, $inv, [ref]$lon) -or
                    -not [double]::TryParse($tok[3], [System.Globalization.NumberStyles]::Float, $inv, [ref]$lat)) {
                    $malformed++
                    if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: non-numeric coordinate line: $line") }
                    continue
                }
                if ($id -gt $maxId) { $maxId = $id }
                if ($lon -lt $minLon) { $minLon = $lon }; if ($lon -gt $maxLon) { $maxLon = $lon }
                if ($lat -lt $minLat) { $minLat = $lat }; if ($lat -gt $maxLat) { $maxLat = $lat }
                if ($null -eq $n) { $errors.Add("line ${lineNo}: coordinate line appears before the 'p' line"); continue }
                if (1 -le $id -and $id -le $n) {
                    if ($present[$id] -ne 0) { $duplicates++ } else { $present[$id] = 1 }
                }
                else { $outOfRange++; if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: node id $id out of range [1,$n]") } }
                continue
            }
            $malformed++
            if ($errors.Count -lt 20) { $errors.Add("line ${lineNo}: unknown record type '$ch': $line") }
        }
    }
    finally { $reader.Dispose() }

    if ($null -eq $headerLine) { $errors.Add("missing 'p aux sp co <n>' problem line") }
    if ($null -ne $n -and $null -ne $Entry -and $n -ne $Entry.Nodes) { $errors.Add("header n=$n != expected $($Entry.Nodes)") }
    if ($vLines -ne $n) { $errors.Add("coordinate line count $vLines != n $n") }
    if ($maxId -ne $n) { $errors.Add("max node id $maxId != n $n") }
    if ($outOfRange -gt 0) { $errors.Add("$outOfRange node ids out of range") }
    if ($duplicates -gt 0) { $errors.Add("$duplicates duplicate node ids") }
    if ($malformed -gt 0) { $errors.Add("$malformed malformed lines") }

    return [pscustomobject]@{
        Ok = ($errors.Count -eq 0)
        Errors = $errors
        Nodes = $n
        Records = $vLines
        MaxId = $maxId
        LonMin = $minLon; LonMax = $maxLon
        LatMin = $minLat; LatMax = $maxLat
        Lines = $lineNo
    }
}

function Write-Manifest {
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($e in $Catalog) {
        if ($e.Kind -eq 'sample') {
            $p = Join-Path $SampleDir $e.RawName
            if (Test-Path -LiteralPath $p) { $lines.Add(('{0}  {1}' -f (Get-Sha256 $p), "samples/$($e.RawName)")) }
        }
        else {
            $raw = Join-Path $RawDir $e.RawName
            $plain = Join-Path $DataDir $e.PlainName
            if (Test-Path -LiteralPath $raw) { $lines.Add(('{0}  {1}' -f (Get-Sha256 $raw), "raw/$($e.RawName)")) }
            if (Test-Path -LiteralPath $plain) { $lines.Add(('{0}  {1}' -f (Get-Sha256 $plain), $e.PlainName)) }
        }
    }
    foreach ($f in @(Get-ChildItem -LiteralPath $QueryDir -File -ErrorAction SilentlyContinue | Sort-Object Name)) {
        $lines.Add(('{0}  {1}' -f (Get-Sha256 $f.FullName), "queries/$($f.Name)"))
    }
    if ($lines.Count -eq 0) { Write-Host '  [manifest] data directory is empty, skipping' -ForegroundColor Yellow; return }
    $sorted = $lines | Sort-Object { ($_ -split '  ', 2)[1] }
    $text = ($sorted -join "`n") + "`n"
    [System.IO.File]::WriteAllText($Manifest, $text, [System.Text.UTF8Encoding]::new($false))
    Write-Host ("  [manifest] {0}  ({1} entries)" -f $Manifest, $sorted.Count) -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
$selected = Select-Entries
Write-Banner ("DIMACS 9th Challenge USA road networks -> {0}" -f $DataDir)
Write-Host ("  Selected {0} files: {1}" -f $selected.Count, (($selected | ForEach-Object { $_.Id }) -join ', '))
Write-Host '  Official site is offline (502/TLS failure); using Wayback archive + GitHub mirror.'
if ($Offline) { Write-Host '  [Offline] no network access, only verify/extract local files.' -ForegroundColor Yellow }

$failures = [System.Collections.Generic.List[string]]::new()
$results = [System.Collections.Generic.List[object]]::new()

foreach ($entry in $selected) {
    Write-Host ''
    Write-Host ("--- {0}  ({1}) ---" -f $entry.Id, $entry.Source) -ForegroundColor White
    try {
        $rawPath = Download-File -Entry $entry
        if ($entry.Kind -eq 'sample') {
            Write-Host ("  [OK]      {0}  ({1})" -f $entry.RawName, (Format-Bytes $entry.Bytes)) -ForegroundColor Green
            $results.Add([pscustomobject]@{ Id = $entry.Id; Kind = 'sample'; Summary = (Format-Bytes $entry.Bytes) })
            continue
        }
        $plainPath = Expand-Dataset -Entry $entry -RawPath $rawPath
        if ($entry.Kind -eq 'gr') {
            $v = Test-DimacsGraph -Path $plainPath -Entry $entry
            if (-not $v.Ok) {
                $failures.Add(("{0}: {1}" -f $entry.Id, ($v.Errors -join '; ')))
                foreach ($err in $v.Errors) { Write-Host ("  [FAIL]    {0}" -f $err) -ForegroundColor Red }
            }
            else {
                $warn = ''
                if ($v.Missing -gt 0) { $warn = "  (warning: {0} nodes never appear in any arc)" -f $v.Missing }
                Write-Host ("  [verify]  n={0} m={1} arc_lines={2} id=[{3},{4}] weight=[{5},{6}] sum={7}{8}" -f `
                    $v.Nodes, $v.Arcs, $v.ArcLines, $v.MinId, $v.MaxId, $v.WeightMin, $v.WeightMax, $v.WeightSum, $warn) -ForegroundColor Green
                $results.Add([pscustomobject]@{ Id = $entry.Id; Kind = 'gr'; Summary = ("n={0} m={1} weight=[{2},{3}]" -f $v.Nodes, $v.Arcs, $v.WeightMin, $v.WeightMax) })
            }
        }
        else {
            $c = Test-DimacsCoords -Path $plainPath -Entry $entry
            if (-not $c.Ok) {
                $failures.Add(("{0}: {1}" -f $entry.Id, ($c.Errors -join '; ')))
                foreach ($err in $c.Errors) { Write-Host ("  [FAIL]    {0}" -f $err) -ForegroundColor Red }
            }
            else {
                Write-Host ("  [verify]  n={0} coord_lines={1} id=[1,{2}] lon=[{3},{4}] lat=[{5},{6}]" -f `
                    $c.Nodes, $c.Records, $c.MaxId, $c.LonMin, $c.LonMax, $c.LatMin, $c.LatMax) -ForegroundColor Green
                $results.Add([pscustomobject]@{ Id = $entry.Id; Kind = 'co'; Summary = ("n={0} coords" -f $c.Nodes) })
            }
        }
    }
    catch {
        $failures.Add(("{0}: {1}" -f $entry.Id, $_.Exception.Message))
        Write-Host ("  [ERROR]   {0}" -f $_.Exception.Message) -ForegroundColor Red
    }
}

# summary
Write-Banner 'SUMMARY'
$results | ForEach-Object { Write-Host ("  [OK]  {0,-16} {1}" -f $_.Id, $_.Summary) -ForegroundColor Green }
if ($failures.Count -gt 0) {
    foreach ($f in $failures) { Write-Host ("  [FAIL] $f") -ForegroundColor Red }
    throw ("{0} file(s) failed verification/download" -f $failures.Count)
}

if (-not $NoManifest) {
    Write-Banner 'MANIFEST'
    Write-Manifest
    Write-Host '  How to verify (pick one):'
    Write-Host '    PowerShell:  (Get-Content data/dimacs/MANIFEST.sha256) | ForEach-Object { $h,$p = $_ -split "  ",2; if ((Get-FileHash ("data/dimacs/"+$p) -Algorithm SHA256).Hash.ToLower() -ne $h) { "FAIL $p" } else { "OK   $p" } }'
    Write-Host '    sha256sum :  cd data/dimacs && sha256sum -c MANIFEST.sha256'
}

Write-Host ''
Write-Host 'Done: data is ready and can be read directly by the C++/Python parsers.' -ForegroundColor Green
Write-Host '  How to generate the query files (1000 p2p pairs, GENERATED, not downloaded officially):'
Write-Host '    python pq-test/tools/gen-p2p-queries.py data/dimacs/USA-road-d.NY.gr'
