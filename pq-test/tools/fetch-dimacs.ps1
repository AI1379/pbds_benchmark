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
      Full    = P0 + P1 + P2 + F (all 48 official USA-road-{d,t} .gr/.co files, ~2.7 GB)
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
    [ValidateSet('All', 'P0', 'P1', 'P2', 'Full', 'Samples')]
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
    [pscustomobject]@{ Id = 'USA.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.USA.gr.gz'; PlainName = 'USA-road-t.USA.gr'; Bytes = 358091759; Sha256 = '95ad9c3e5b1ca592c8b7f85726aad5a00412631b8bbb7290e3d65648abf1d867'; PlainBytes = 1409628748; PlainSha256 = '70cdf42934bd9e9277ae8ecc08ef8d4561e0441adfd8d4f46159e62e7eaed186'; Nodes = 23947347; Arcs = 58333344; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.USA.gr.gz'; WaybackTimestamp = '20120206064312'; Urls = @('https://web.archive.org/web/20120206064312id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.USA.gr.gz'); Source = 'Wayback Machine official archive (2012-02-06 capture; the 2011 capture is truncated)' },
    [pscustomobject]@{ Id = 'USA.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.USA.gr.gz'; PlainName = 'USA-road-d.USA.gr'; Bytes = 351265214; Sha256 = '2b220d87d32171032d6afe6120c5ee05a5f7f82e36fc192b5108c281cb10a694'; PlainBytes = 1387075114; PlainSha256 = '8380c1e95c70dff3d76753ebe571b52d6682963e4a78858a7c660b50c188739a'; Nodes = 23947347; Arcs = 58333344; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.USA.gr.gz'; WaybackTimestamp = '20230413132821'; Urls = @('https://web.archive.org/web/20230413132821id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.USA.gr.gz'); Source = 'Wayback Machine official archive (2023 new-domain capture)' },
    [pscustomobject]@{ Id = 'USA.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.USA.co.gz'; PlainName = 'USA-road-t.USA.co'; Bytes = 228875218; Sha256 = 'd75ddf2837926361ecc9c75b37ac3d4bc9b7a0b6d17104ccee40ea08dd6003a7'; PlainBytes = 713572087; PlainSha256 = '5d1d1ec621bc449b3c1224a8ed5c36d6133311f790060843c52598ee1dd57d31'; Nodes = 23947347; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.USA.co.gz'; WaybackTimestamp = '20110607014542'; Urls = @('https://web.archive.org/web/20110607014542id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.USA.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'USA.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.USA.co.gz'; PlainName = 'USA-road-d.USA.co'; Bytes = 228875218; Sha256 = '2a25ecdb10e1ff9fcc93562ecebe237b2cafbb478f5ded5d95af921ddfff8761'; PlainBytes = 713572087; PlainSha256 = '79a931153ed5ea6da345201e62f1e96042cbec0a2833181a9086b400e4e900a1'; Nodes = 23947347; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.USA.co.gz'; WaybackTimestamp = '20110606175909'; Urls = @('https://web.archive.org/web/20110606175909id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.USA.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CTR.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.CTR.gr.gz'; PlainName = 'USA-road-t.CTR.gr'; Bytes = 207873899; Sha256 = '4155606abbbf563a989fd06b2cb3aa11a0abb83f91d92eb67ad837c335deced1'; PlainBytes = 806095856; PlainSha256 = '8c2aa5aa78aa8a103cc55e9495e64b2fefcbf3e261809714c9c8573edaa95f65'; Nodes = 14081816; Arcs = 34292496; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CTR.gr.gz'; WaybackTimestamp = '20110606174134'; Urls = @('https://web.archive.org/web/20110606174134id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CTR.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CTR.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.CTR.gr.gz'; PlainName = 'USA-road-d.CTR.gr'; Bytes = 204599279; Sha256 = 'd6258a198e3fad10492c4cae72150bf26387ae123734348d19586c77d97e6e41'; PlainBytes = 793104704; PlainSha256 = '8bb47914ded8dfea0e88c613444e50af74aa29a411e80f8970a5a3fa44ae447c'; Nodes = 14081816; Arcs = 34292496; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CTR.gr.gz'; WaybackTimestamp = '20110606165843'; Urls = @('https://web.archive.org/web/20110606165843id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CTR.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CTR.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.CTR.co.gz'; PlainName = 'USA-road-t.CTR.co'; Bytes = 145471697; Sha256 = 'aa788c686e69bf6ef05d193b2f007b923b3f54bbe29b2a06cfbf304effce12d8'; PlainBytes = 411343581; PlainSha256 = '3b6cc1fe93513425bf1c36842344cd74e7e840c5275b15011eec0515c9d119d7'; Nodes = 14081816; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CTR.co.gz'; WaybackTimestamp = '20110607020623'; Urls = @('https://web.archive.org/web/20110607020623id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CTR.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CTR.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.CTR.co.gz'; PlainName = 'USA-road-d.CTR.co'; Bytes = 145471697; Sha256 = '56e859e4a59df9a8359343d1d739c15bfdd0707916d4accc6fc3495bc885e816'; PlainBytes = 411343581; PlainSha256 = '5f00129e4715d4551cff38eeeda3e7569ef16e6c25e8a48871577f765223087a'; Nodes = 14081816; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CTR.co.gz'; WaybackTimestamp = '20110606202958'; Urls = @('https://web.archive.org/web/20110606202958id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CTR.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'W.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.W.gr.gz'; PlainName = 'USA-road-t.W.gr'; Bytes = 92451633; Sha256 = '9a4d7e6dd32d6c011c186fb48b674e3c085f69a37d19ace293941eeb5b5692a4'; PlainBytes = 347817240; PlainSha256 = '6b692758f9ec7ecc72ce07818a9a3a26cba1b1a05e13ea27c9b103e11dc98dc4'; Nodes = 6262104; Arcs = 15248146; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.W.gr.gz'; WaybackTimestamp = '20110606185151'; Urls = @('https://web.archive.org/web/20110606185151id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.W.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'W.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.W.gr.gz'; PlainName = 'USA-road-d.W.gr'; Bytes = 90580736; Sha256 = 'aeb7d846467dac4945b760c9d6a30fff46a743fb71c9d1256dfb97da471fb42d'; PlainBytes = 341662944; PlainSha256 = '6ea97c6bbcbe20de50d2d9224bde5acea208c02d5b11abd054a0b7d059ae3ad3'; Nodes = 6262104; Arcs = 15248146; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.W.gr.gz'; WaybackTimestamp = '20110606173250'; Urls = @('https://web.archive.org/web/20110606173250id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.W.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'W.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.W.co.gz'; PlainName = 'USA-road-d.W.co'; Bytes = 60272281; Sha256 = '1f6e0974e5498573c793cdb81507e0ecaf992575350ee97349493537f2505e11'; PlainBytes = 186752215; PlainSha256 = 'a6a56ed25a23173f6ae34e768709ca89dcc9339dc3f3d23e07476caca08f0663'; Nodes = 6262104; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.W.co.gz'; WaybackTimestamp = '20110606172715'; Urls = @('https://web.archive.org/web/20110606172715id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.W.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'W.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.W.co.gz'; PlainName = 'USA-road-t.W.co'; Bytes = 60272280; Sha256 = '5da73d84c1942b17a30c666794bfc90369e8852e87c54effc0ac1216a9f6cd56'; PlainBytes = 186752215; PlainSha256 = 'cc88b8ea95fd17e0308ecac75a3c9f6265ecdf324ca227f8e6e7e3a14e049342'; Nodes = 6262104; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.W.co.gz'; WaybackTimestamp = '20110607011913'; Urls = @('https://web.archive.org/web/20110607011913id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.W.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'E.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.E.gr.gz'; PlainName = 'USA-road-t.E.gr'; Bytes = 52767789; Sha256 = '9d8e64a6c793cfbcd27456631d10974fda5a3c8b3bd3ef2f230f42748c3ac1ff'; PlainBytes = 196426056; PlainSha256 = 'e950c51a8425529f722a6c93f8fd1293bc127b6f9f7bec6a4bffeb6f5114fc39'; Nodes = 3598623; Arcs = 8778114; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.E.gr.gz'; WaybackTimestamp = '20110606164410'; Urls = @('https://web.archive.org/web/20110606164410id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.E.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'LKS.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.LKS.gr.gz'; PlainName = 'USA-road-t.LKS.gr'; Bytes = 41088084; Sha256 = 'c558a091c0eb0f710892aa61b0707eecffe0eb011d750f99dd05ab55b5d03bde'; PlainBytes = 153331546; PlainSha256 = 'fe62e1550deaeb23f68e59eb79d9502ce7f45cb5d24aad7722d79f29209354f9'; Nodes = 2758119; Arcs = 6885658; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.LKS.gr.gz'; WaybackTimestamp = '20110606181828'; Urls = @('https://web.archive.org/web/20110606181828id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.LKS.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'LKS.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.LKS.gr.gz'; PlainName = 'USA-road-d.LKS.gr'; Bytes = 40257451; Sha256 = '72c203400c5bc0e7e87ff0a6081048f8dc5bb472c696fad2e880ce2cc976d2b4'; PlainBytes = 150712428; PlainSha256 = '1859a822a47e60b20b7b4c380536f7e89a4a8b9ec8db300836e705cccbc5cc61'; Nodes = 2758119; Arcs = 6885658; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.LKS.gr.gz'; WaybackTimestamp = '20110606183612'; Urls = @('https://web.archive.org/web/20110606183612id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.LKS.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'E.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.E.co.gz'; PlainName = 'USA-road-t.E.co'; Bytes = 33386839; Sha256 = '515a852920214160a2bfc2aeb4d7720a91129dc8759fc7c774d8657f15f04c6e'; PlainBytes = 103249162; PlainSha256 = 'a5fc8a2c8f26e4aacc9c5c8888fa3b0f1f87b1fc6ed8696d8f32620687dd0bf6'; Nodes = 3598623; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.E.co.gz'; WaybackTimestamp = '20110607021840'; Urls = @('https://web.archive.org/web/20110607021840id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.E.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'E.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.E.co.gz'; PlainName = 'USA-road-d.E.co'; Bytes = 33386839; Sha256 = 'b85c36a7a399952fc5265a1195a8df6e6d5f6fa2443b7387eee1cdb2548e1533'; PlainBytes = 103249162; PlainSha256 = 'bdbe44cd1d71f19e3b9dd0298d88afa78f303c3acc97d341d3fa84ef3c49d177'; Nodes = 3598623; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.E.co.gz'; WaybackTimestamp = '20110606172415'; Urls = @('https://web.archive.org/web/20110606172415id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.E.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CAL.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.CAL.gr.gz'; PlainName = 'USA-road-t.CAL.gr'; Bytes = 27544887; Sha256 = '5233b9eb63563440afdb07f34b6c9355f643321ea3e43439455f2f85dc3c35bc'; PlainBytes = 101953346; PlainSha256 = 'e6d547658d5242ec74930645fa81c11822871a88400b883aeee7ad051fdb6426'; Nodes = 1890815; Arcs = 4657742; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CAL.gr.gz'; WaybackTimestamp = '20110607013855'; Urls = @('https://web.archive.org/web/20110607013855id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CAL.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CAL.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.CAL.gr.gz'; PlainName = 'USA-road-d.CAL.gr'; Bytes = 26974023; Sha256 = 'bb79eba9185ae399f2d885c4d71ca7e42d39787d9eee66b9bf579e5d747f0d76'; PlainBytes = 100040118; PlainSha256 = '009c1395342fcdc7dc28e8b5785fd56d24999deec0571f8a8ab5a6ef02c32ee4'; Nodes = 1890815; Arcs = 4657742; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CAL.gr.gz'; WaybackTimestamp = '20110606184006'; Urls = @('https://web.archive.org/web/20110606184006id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CAL.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'LKS.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.LKS.co.gz'; PlainName = 'USA-road-d.LKS.co'; Bytes = 25493420; Sha256 = 'dc43a4191c48a197f79ec2b6d9b6ecfec03097d6a32a94607b45d0455246d2f4'; PlainBytes = 78874548; PlainSha256 = '98af029fe29462d6740a51e2438e5086493d5fd758ac67ee2e46c7fce0d7233f'; Nodes = 2758119; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.LKS.co.gz'; WaybackTimestamp = '20110606171702'; Urls = @('https://web.archive.org/web/20110606171702id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.LKS.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'LKS.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.LKS.co.gz'; PlainName = 'USA-road-t.LKS.co'; Bytes = 25493419; Sha256 = 'e96d02b2629a215a4518c48cb728c976dffe30b2843e3eab485e7c6da9c3341c'; PlainBytes = 78874548; PlainSha256 = '98b6d96950b39e6c465f65aa62defad92791eb7fdc118a4f7940b92fd6ebd7ac'; Nodes = 2758119; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.LKS.co.gz'; WaybackTimestamp = '20110607023337'; Urls = @('https://web.archive.org/web/20110607023337id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.LKS.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NE.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.NE.gr.gz'; PlainName = 'USA-road-t.NE.gr'; Bytes = 22256415; Sha256 = '30337675d9c63fe2fc809bfa7afe3708d581467c3243a76e8d44a522e0c96b47'; PlainBytes = 83803671; PlainSha256 = 'e2555a66dc2caebb1d4af48b41d8c809efbe644a04e150824270df38ad33d558'; Nodes = 1524453; Arcs = 3897636; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NE.gr.gz'; WaybackTimestamp = '20110607012915'; Urls = @('https://web.archive.org/web/20110607012915id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NE.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CAL.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.CAL.co.gz'; PlainName = 'USA-road-d.CAL.co'; Bytes = 17195534; Sha256 = '75b13daffc71efc1baf41c7276218c323f29f3d2237da8688e261a1bf9606fe4'; PlainBytes = 55613547; PlainSha256 = '62b99c0268e4dfc2006de2f19df289b07e08c5c3d2c5b8a0838d2fcb0bbd5575'; Nodes = 1890815; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CAL.co.gz'; WaybackTimestamp = '20110607031757'; Urls = @('https://web.archive.org/web/20110607031757id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.CAL.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'CAL.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.CAL.co.gz'; PlainName = 'USA-road-t.CAL.co'; Bytes = 17195534; Sha256 = '37a72c5a258ecb1fd54697f5292538d5c9a56c1f3f12136a48e0e614dd8132a9'; PlainBytes = 55613547; PlainSha256 = '7ac326915b6f98bbf89c7ac56a28348a960fedf9c8fcf6e3c29b1b0d6b332d30'; Nodes = 1890815; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CAL.co.gz'; WaybackTimestamp = '20110607011549'; Urls = @('https://web.archive.org/web/20110607011549id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.CAL.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NW.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.NW.gr.gz'; PlainName = 'USA-road-t.NW.gr'; Bytes = 16335658; Sha256 = '2136d05b33374ede4b6d8b6bb48e5bb8152eb4ff39b45c9660cda4cd605c6399'; PlainBytes = 60487739; PlainSha256 = '937fd56db30ce86719d77cde8f4ac228535e9ca16a27063055e7665c7693e124'; Nodes = 1207945; Arcs = 2840208; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NW.gr.gz'; WaybackTimestamp = '20110607014401'; Urls = @('https://web.archive.org/web/20110607014401id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NW.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NW.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.NW.gr.gz'; PlainName = 'USA-road-d.NW.gr'; Bytes = 15932705; Sha256 = 'aa529ed44349bcdafe6e7b16d47f896884beeb9db49701f221a05cfec7896cbc'; PlainBytes = 59301489; PlainSha256 = '27c852516589efa0cf1166cbdcf1a817853157822124498bf2adefc1216f7dd4'; Nodes = 1207945; Arcs = 2840208; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NW.gr.gz'; WaybackTimestamp = '20110607032131'; Urls = @('https://web.archive.org/web/20110607032131id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NW.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'FLA.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.FLA.gr.gz'; PlainName = 'USA-road-t.FLA.gr'; Bytes = 14807799; Sha256 = 'a316684184e78276c40828cdb2b5c098b315a9eb2f71a8f02e6220973af87658'; PlainBytes = 56703854; PlainSha256 = '89bfa1b128c3ae6b584c216fa41ee754db93cf2eee89592b6ee2465a20996c88'; Nodes = 1070376; Arcs = 2712798; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.FLA.gr.gz'; WaybackTimestamp = '20110607011413'; Urls = @('https://web.archive.org/web/20110607011413id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.FLA.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'FLA.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.FLA.gr.gz'; PlainName = 'USA-road-d.FLA.gr'; Bytes = 14448759; Sha256 = 'a42e4e71e3d46eaf9a1d33270aa69d52207b75521525774bde1ac4c8fe804d47'; PlainBytes = 55645328; PlainSha256 = '47a3e851dea2bf27d6c939542e569fa9b5e6f2c8f11e8af3187d87c28435a85d'; Nodes = 1070376; Arcs = 2712798; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.FLA.gr.gz'; WaybackTimestamp = '20110607033240'; Urls = @('https://web.archive.org/web/20110607033240id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.FLA.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NE.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.NE.co.gz'; PlainName = 'USA-road-t.NE.co'; Bytes = 13303203; Sha256 = '354f555972defa01878b7173c8148e270892406d0277f96ab9f2f5a87453da4e'; PlainBytes = 43098233; PlainSha256 = '13821510df7dfd9ff0568e01497378c5c26eb5b45d9d4ed11fba65a75240b136'; Nodes = 1524453; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NE.co.gz'; WaybackTimestamp = '20110607022211'; Urls = @('https://web.archive.org/web/20110607022211id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NE.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NE.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.NE.co.gz'; PlainName = 'USA-road-d.NE.co'; Bytes = 13303203; Sha256 = 'f6e08653728f52626641dffc8274f36221106f7e154054e09327cdc19a3e73b3'; PlainBytes = 43098233; PlainSha256 = '023bfaa694147f625a2ecadf36da67895c334202410b420b9c3f083c05076235'; Nodes = 1524453; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NE.co.gz'; WaybackTimestamp = '20110607033810'; Urls = @('https://web.archive.org/web/20110607033810id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NE.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NW.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.NW.co.gz'; PlainName = 'USA-road-t.NW.co'; Bytes = 11293224; Sha256 = 'b6da3dcefef6274e31e21ead80ed08cf2116ad1b2aada8babba5f84032e3d6cf'; PlainBytes = 35127446; PlainSha256 = 'd6383ff6eb6601ee5b94948829af3f3a836a69db9fb5f8aeb51f1524363b6e21'; Nodes = 1207945; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NW.co.gz'; WaybackTimestamp = '20110607011006'; Urls = @('https://web.archive.org/web/20110607011006id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NW.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NW.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.NW.co.gz'; PlainName = 'USA-road-d.NW.co'; Bytes = 11293224; Sha256 = 'b09cc1ba1458325543a73b5909d162be43cf01e79f3f99b94cabc1bd8e7b5c4c'; PlainBytes = 35127446; PlainSha256 = '7bcfc673516bf57d23aff5fe5170ab361d770db6a039fd16b358c64cf87a23b0'; Nodes = 1207945; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NW.co.gz'; WaybackTimestamp = '20110607032922'; Urls = @('https://web.archive.org/web/20110607032922id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.NW.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'FLA.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.FLA.co.gz'; PlainName = 'USA-road-t.FLA.co'; Bytes = 9062784; Sha256 = '55d69f0abc8314e367b44503873a5020026a6fbd2b8627e5832e18bdc4864a2e'; PlainBytes = 29930001; PlainSha256 = 'ed1a28d9b81746f16677c45ffafa091a5266098e0e4c17fc2178ee0383e571cc'; Nodes = 1070376; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.FLA.co.gz'; WaybackTimestamp = '20110607023137'; Urls = @('https://web.archive.org/web/20110607023137id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.FLA.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'FLA.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.FLA.co.gz'; PlainName = 'USA-road-d.FLA.co'; Bytes = 9062784; Sha256 = 'd81d48c37a02b7e92bc658de0c8c6b18bb81f8ce381f9ed5665b4f6d0cded131'; PlainBytes = 29930001; PlainSha256 = 'e9ef8ff417f2e0b19faf51918b87f7e77d99269fa95726d105fdf47cd893e90d'; Nodes = 1070376; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.FLA.co.gz'; WaybackTimestamp = '20110607032341'; Urls = @('https://web.archive.org/web/20110607032341id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.FLA.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'COL.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.COL.gr.gz'; PlainName = 'USA-road-t.COL.gr'; Bytes = 5899887; Sha256 = '0369412263d1939a1f3c142fd9dda691253fc67591ab7f0ec017a54f4535cf48'; PlainBytes = 21759502; PlainSha256 = '5f54f0e5df788d05d930664771b281c18b8d49183a47698ee31a20f22f43bf48'; Nodes = 435666; Arcs = 1057066; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.COL.gr.gz'; WaybackTimestamp = '20110607011307'; Urls = @('https://web.archive.org/web/20110607011307id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.COL.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'COL.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-d.COL.gr.gz'; PlainName = 'USA-road-d.COL.gr'; Bytes = 5753354; Sha256 = '638bb7412a943b95d0a39211fa741c9c54a8a55a3992281175f950b08a983543'; PlainBytes = 21344630; PlainSha256 = '2d5510bdc742cf027c0ce90f5d9eebeace29c7431f54f31be015d2ddac070343'; Nodes = 435666; Arcs = 1057066; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.COL.gr.gz'; WaybackTimestamp = '20110607033105'; Urls = @('https://web.archive.org/web/20110607033105id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.COL.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'BAY.t.gr'; Priority = 'F'; Kind = 'gr'; RawName = 'USA-road-t.BAY.gr.gz'; PlainName = 'USA-road-t.BAY.gr'; Bytes = 4237427; Sha256 = '85d597078d13907e60ff826d8c7fe0b18ca4c690d003751e27e30ee4011091c3'; PlainBytes = 16206846; PlainSha256 = '272f13715fa70a0d029193fa8cac70f4510fa4cadfd8d96b14198f19df20ac75'; Nodes = 321270; Arcs = 800172; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.BAY.gr.gz'; WaybackTimestamp = '20110607023638'; Urls = @('https://web.archive.org/web/20110607023638id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.BAY.gr.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'COL.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.COL.co.gz'; PlainName = 'USA-road-t.COL.co'; Bytes = 3963158; Sha256 = 'd171d7d695729fbebe9def3f24e569ff0664e32efa488db525d298fb4c3b0713'; PlainBytes = 12523408; PlainSha256 = 'dfc0a36c4337cb4f745a5b0edc24f485f0ac19ae20ab028008cf1e4f01ad2785'; Nodes = 435666; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.COL.co.gz'; WaybackTimestamp = '20110607022945'; Urls = @('https://web.archive.org/web/20110607022945id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.COL.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'COL.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.COL.co.gz'; PlainName = 'USA-road-d.COL.co'; Bytes = 3963158; Sha256 = '6a88edbeaf55aa0b0f82daa528e9a3ca91c1beb16f6a3970d7908a49ab013a7f'; PlainBytes = 12523408; PlainSha256 = '1c545e75ef66d25fb8a2478a5395b3b0bd16e7fb463236a97f25718c89226ba4'; Nodes = 435666; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.COL.co.gz'; WaybackTimestamp = '20110607033702'; Urls = @('https://web.archive.org/web/20110607033702id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.COL.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'BAY.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.BAY.co.gz'; PlainName = 'USA-road-t.BAY.co'; Bytes = 2581007; Sha256 = 'f55192a223c5013f9891546e2fd6bc1ea3a5b0907a42362d19b61f3fd7ec6547'; PlainBytes = 9205924; PlainSha256 = '96634beddc5ebb587161f9b4a6a59762f97c47b01c1dfe678b3750a5ecb56dfb'; Nodes = 321270; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.BAY.co.gz'; WaybackTimestamp = '20110607020421'; Urls = @('https://web.archive.org/web/20110607020421id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.BAY.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'BAY.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-d.BAY.co.gz'; PlainName = 'USA-road-d.BAY.co'; Bytes = 2581007; Sha256 = '67330855e3082dae03609f5b3012fe13ad2d982139d79c3040e77dad55ffd338'; PlainBytes = 9205924; PlainSha256 = '5446a3b07e99a6fbf2aa091bd7095b55e4ba77bb94ac9865133b6b9af98af153'; Nodes = 321270; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.BAY.co.gz'; WaybackTimestamp = '20110607032806'; Urls = @('https://web.archive.org/web/20110607032806id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-d/USA-road-d.BAY.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
    [pscustomobject]@{ Id = 'NY.t.co'; Priority = 'F'; Kind = 'co'; RawName = 'USA-road-t.NY.co.gz'; PlainName = 'USA-road-t.NY.co'; Bytes = 2058598; Sha256 = '1f26eedeb437ec140f5b70c40e3e60bedb07973252972cbbd2e8b3fc8f37b95d'; PlainBytes = 7290781; PlainSha256 = '826a89d357c78afeacc931f8dc2de00565a6dac8306569d86cbac346f5bcfe95'; Nodes = 264346; Arcs = $null; WeightMin = $null; WeightMax = $null; OfficialUrl = 'http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NY.co.gz'; WaybackTimestamp = '20110607012819'; Urls = @('https://web.archive.org/web/20110607012819id_/http://www.dis.uniroma1.it/~challenge9/data/USA-road-t/USA-road-t.NY.co.gz'); Source = 'Wayback Machine official archive (2011 old-domain capture)' },
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
        'Full' { @('P0', 'P1', 'P2', 'F') }
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
