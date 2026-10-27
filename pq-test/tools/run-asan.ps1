<#
.SYNOPSIS
    Build and run C++ code with Clang AddressSanitizer + UndefinedBehaviorSanitizer.

.DESCRIPTION
    Why this script exists (verified on this machine, 2026-10-27):

      MSYS2's MinGW/Cygwin toolchains cannot link sanitizer runtimes:
        * mingw-w64-ucrt-x86_64-gcc 16.2.0 was built without libsanitizer;
          `g++ -print-file-name=libasan.a` prints `libasan` (i.e. not found).
        * mingw-w64-ucrt-x86_64-clang 23.1.3 + compiler-rt: the package ships
          only builtins/fuzzer/profile/orc, there is no libclang_rt.asan* /
          libclang_rt.ubsan* for the *-windows-gnu target.
        * msys/clang + msys/compiler-rt (Cygwin target): sanitizer headers
          only, no runtimes.
      Upstream does not support ASan/UBSan for the MinGW/Cygwin targets, so
      `pacman -S` cannot provide them.

      The working toolchain here is the LLVM clang with the MSVC target
      triple (x86_64-pc-windows-msvc); its sanitizer runtime DLLs/libs ship
      in <LLVM>\lib\clang\<version>\lib\windows. Visual Studio provides the
      linker and the MSVC / Windows SDK libraries. On this machine that LLVM
      is the one installed by scoop (`scoop install llvm`), clang 21.x.

    This script locates that LLVM, builds with
        -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
    and runs the binary with the ASan runtime DLL directory on PATH.
    TEMP/TMP are redirected to <OutDir>\tmp: gtest death tests create temporary
    files via GetTempFileNameA, which can otherwise fail with error 5.

    Default target: pq-test/heap-adaptor-test.cpp plus the vendored GoogleTest
    from third_party/googletest.

.PARAMETER Sources
    Source files to compile. Default: pq-test/heap-adaptor-test.cpp.
    Relative paths are resolved against the current directory.

.PARAMETER Includes
    Extra include directories (-I). pq-test/ is always added; for the default
    gtest target the vendored gtest include dirs are added as well.

.PARAMETER OutDir
    Build directory. Default: <proj2>/.cache/asan (git-ignored).

.PARAMETER Name
    Output executable name without extension. Default: asan-<first source>.

.PARAMETER WithGtest
    Compile and link the vendored GoogleTest (gtest-all.cc + gtest_main.cc).
    Defaults to true for the default target, false for custom -Sources.

.PARAMETER GtestFilter
    Passed as --gtest_filter=<value>, e.g. 'HeapAdaptorTest/2.*' or
    'SegmentTreeHeap*:HeapAdaptorTest/0.*:HeapAdaptorTest/1.*'.

.PARAMETER TimeoutSec
    Kill the test process after this many seconds (default 120) and report a
    timeout. Useful while FibonacciHeap still hangs (pop never drains the heap).

.PARAMETER LlvmRoot
    LLVM installation root (contains bin\clang++.exe). Default lookup order:
    this parameter, $env:LLVM_ROOT, clang++ on PATH, scoop's
    %USERPROFILE%\scoop\apps\llvm\current, C:\Program Files\LLVM.

.PARAMETER Sanitize
    Sanitizer list passed to -fsanitize, default 'address,undefined'.
    Use 'address' alone if UBSan flags a third-party header.

.PARAMETER ExtraArgs
    Extra compiler arguments appended verbatim (e.g. -DUSE_ORIGINAL, -Wno-...).

.PARAMETER NoRun
    Build only, do not run.

.EXAMPLE
    pwsh -File tools/run-asan.ps1
    # Full heap-adaptor-test built with ASan+UBSan and run.

.EXAMPLE
    pwsh -File tools/run-asan.ps1 -GtestFilter 'SegmentTreeHeap*:HeapAdaptorTest/0.*:HeapAdaptorTest/1.*'
    # Only the heaps that currently work (skips the hanging FibonacciHeap tests).

.EXAMPLE
    pwsh -File tools/run-asan.ps1 -GtestFilter 'HeapAdaptorTest/2.PopsInNondecreasingOrder' -TimeoutSec 10
    # Prove that the current FibonacciHeap hangs in extract-min (exit code 124).

.EXAMPLE
    pwsh -File tools/run-asan.ps1 -Sources ..\..\..\.ads-tmp\fibaudit-lead\audit.cpp `
        -Includes ..\..\..\ads-projects\proj2\pq-test -Name fib-audit
    # Build a custom standalone harness (its own main()) with the sanitizers.
#>
[CmdletBinding()]
param(
    [string[]]$Sources = @(),
    [string[]]$Includes = @(),
    [string]$OutDir = '',
    [string]$Name = '',
    [string]$GtestFilter = '',
    [Nullable[bool]]$WithGtest = $null,
    [int]$TimeoutSec = 120,
    [string]$LlvmRoot = '',
    [string]$Sanitize = 'address,undefined',
    [string[]]$ExtraArgs = @(),
    [switch]$NoRun
)

$ErrorActionPreference = 'Stop'

function Test-LlvmRoot([string]$path) {
    if (-not $path) { return $false }
    return (Test-Path (Join-Path $path 'bin\clang++.exe'))
}

function Find-LlvmRoot([string]$explicit) {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($explicit) { $candidates.Add($explicit) }
    if ($env:LLVM_ROOT) { $candidates.Add($env:LLVM_ROOT) }
    $cmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) {
        $candidates.Add((Split-Path (Split-Path $cmd.Source -Parent) -Parent))
    }
    if ($env:USERPROFILE) {
        $candidates.Add((Join-Path $env:USERPROFILE 'scoop\apps\llvm\current'))
    }
    $candidates.Add('C:\Program Files\LLVM')
    foreach ($c in $candidates) {
        if (Test-LlvmRoot $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Find-AsanRuntimeDir([string]$root) {
    $clangLib = Join-Path $root 'lib\clang'
    if (-not (Test-Path $clangLib)) { return $null }
    $dirs = Get-ChildItem $clangLib -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($d in $dirs) {
        $win = Join-Path $d.FullName 'lib\windows'
        if (Test-Path (Join-Path $win 'clang_rt.asan_dynamic-x86_64.dll')) {
            return $win
        }
    }
    return $null
}

$PqTest = Split-Path -Parent $PSScriptRoot
$Proj2 = Split-Path -Parent $PqTest
$GtestRoot = Join-Path $Proj2 'third_party\googletest\googletest'

$defaultTarget = ($Sources.Count -eq 0)
if ($defaultTarget) {
    $Sources = @(Join-Path $PqTest 'heap-adaptor-test.cpp')
}
if ($null -eq $WithGtest) {
    $WithGtest = $defaultTarget
}

$resolvedSources = @()
foreach ($s in $Sources) {
    if (-not (Test-Path $s)) {
        $rp = Resolve-Path $s -ErrorAction SilentlyContinue
        if (-not $rp) { throw "source file not found: $s" }
        $s = $rp.Path
    }
    $resolvedSources += (Resolve-Path $s).Path
}

$resolvedIncludes = New-Object System.Collections.Generic.List[string]
$resolvedIncludes.Add((Resolve-Path $PqTest).Path)
foreach ($i in $Includes) {
    $rp = Resolve-Path $i -ErrorAction SilentlyContinue
    if (-not $rp) { throw "include directory not found: $i" }
    if (-not $resolvedIncludes.Contains($rp.Path)) {
        $resolvedIncludes.Add($rp.Path)
    }
}

$root = Find-LlvmRoot $LlvmRoot
if (-not $root) {
    throw @'
LLVM (MSVC target) not found. Install it, e.g.
    scoop install llvm
or pass -LlvmRoot <dir containing bin\clang++.exe>.
MSYS2's own clang/gcc cannot be used: they target MinGW and have no sanitizer
runtimes (see this script's .DESCRIPTION).
'@
}
$clang = Join-Path $root 'bin\clang++.exe'
$triple = (& $clang -print-target-triple 2>$null) -join ''
if ($triple -notmatch 'msvc') {
    throw "clang at $clang targets '$triple'; AddressSanitizer needs the MSVC target (*-pc-windows-msvc). Pass a different -LlvmRoot (scoop's LLVM works)."
}

$rtDir = Find-AsanRuntimeDir $root
if (-not $rtDir) {
    throw "ASan runtime not found under $root\lib\clang\*\lib\windows. Install a full LLVM release (scoop install llvm)."
}

if (-not $OutDir) {
    $OutDir = Join-Path $Proj2 '.cache\asan'
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
$tmpDir = Join-Path $OutDir 'tmp'
New-Item -ItemType Directory -Force $tmpDir | Out-Null

$env:PATH = $rtDir + ';' + (Join-Path $root 'bin') + ';' + $env:PATH
$env:TEMP = (Resolve-Path $tmpDir).Path
$env:TMP = $env:TEMP
$env:ASAN_OPTIONS = 'detect_leaks=0:abort_on_error=0:symbolize=1'
$env:UBSAN_OPTIONS = 'print_stacktrace=1:halt_on_error=0'

if (-not $Name) {
    $Name = 'asan-' + [IO.Path]::GetFileNameWithoutExtension($resolvedSources[0])
}
$exe = Join-Path (Resolve-Path $OutDir).Path ($Name + '.exe')

$compileArgs = @(
    '-std=c++20', '-O1', '-g',
    ('-fsanitize=' + $Sanitize),
    '-fno-omit-frame-pointer',
    '-fno-sanitize-recover=all',
    '-Wall', '-Wextra'
)
foreach ($i in $resolvedIncludes) { $compileArgs += @('-I', $i) }
foreach ($s in $resolvedSources) { $compileArgs += $s }
$compileArgs += $ExtraArgs
if ($WithGtest) {
    $compileArgs += @(
        '-I', (Join-Path $GtestRoot 'include'),
        '-I', $GtestRoot,
        (Join-Path $GtestRoot 'src\gtest-all.cc'),
        (Join-Path $GtestRoot 'src\gtest_main.cc')
    )
}
$compileArgs += @('-o', $exe)

Write-Host "clang++  : $clang ($triple)"
Write-Host "asan lib : $rtDir"
Write-Host "build    : $exe"
Write-Host "sanitize : $Sanitize"
Write-Host ""

& $clang @compileArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED (exit code $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}
Write-Host "build OK"

if ($NoRun) { exit 0 }

$runArgs = @()
if ($GtestFilter) { $runArgs += "--gtest_filter=$GtestFilter" }

$stdoutFile = Join-Path (Resolve-Path $OutDir).Path ($Name + '.stdout.txt')
$stderrFile = Join-Path (Resolve-Path $OutDir).Path ($Name + '.stderr.txt')
Remove-Item $stdoutFile, $stderrFile -ErrorAction SilentlyContinue

Write-Host "run      : $exe $($runArgs -join ' ')  (timeout ${TimeoutSec}s)"
Write-Host ("-" * 72)
$proc = Start-Process -FilePath $exe -ArgumentList $runArgs -PassThru -NoNewWindow `
    -WorkingDirectory (Resolve-Path $OutDir).Path `
    -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile

$finished = $proc.WaitForExit($TimeoutSec * 1000)
if (-not $finished) {
    try { $proc.Kill($true) } catch { }
    $proc.WaitForExit()
    $exitCode = 124
    Write-Host "TIMEOUT after $TimeoutSec s - process killed." -ForegroundColor Yellow
} else {
    $exitCode = $proc.ExitCode
}

if (Test-Path $stdoutFile) { Get-Content $stdoutFile }
if (Test-Path $stderrFile) { Get-Content $stderrFile }
Write-Host ("-" * 72)
if ($exitCode -eq 0) {
    Write-Host "PASSED (exit code 0)" -ForegroundColor Green
} elseif ($exitCode -eq 124) {
    Write-Host "TIMEOUT (exit code 124)" -ForegroundColor Yellow
} else {
    Write-Host "FAILED (exit code $exitCode)" -ForegroundColor Red
}
exit $exitCode
