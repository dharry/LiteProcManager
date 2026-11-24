<#
.SYNOPSIS
    Automated static heuristic scan, binary security inspection, and unit test runner for LiteProcManager.
.DESCRIPTION
    Assists the code-check skill by scanning src/, include/, and tests/ for common Win32 handle leaks,
    memory/GDI lifecycle hazards, PID reuse/TOCTOU patterns, binary hardening flags, and unit test status.
#>
param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Continue"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")
Set-Location $RepoRoot

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " LiteProcManager Code Audit & Leak/Security Scanner" -ForegroundColor Cyan
Write-Host " Workspace: $RepoRoot" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# 1. Static Heuristic Counts (Allocation vs Deallocation pairs)
Write-Host "`n[1/4] Win32 Resource Allocation / Cleanup Balance (src/ & include/)" -ForegroundColor Yellow

$SourceFiles = Get-ChildItem -Path "$RepoRoot\src", "$RepoRoot\include" -Include *.cc, *.h -Recurse

function Get-PatternMatches {
    param([string]$Pattern)
    $matchesList = @()
    foreach ($file in $SourceFiles) {
        $res = Select-String -Path $file.FullName -Pattern $Pattern
        if ($res) { $matchesList += $res }
    }
    return $matchesList
}

$ResourcePairs = @(
    @{ Category = "Kernel Handles"; Alloc = "\b(OpenProcess|OpenProcessToken|CreateToolhelp32Snapshot|CreateFileW|CreateEventW|CreateMutexW)\s*\("; Free = "\bCloseHandle\s*\(" },
    @{ Category = "Service Handles"; Alloc = "\b(OpenSCManagerW|OpenServiceW)\s*\("; Free = "\bCloseServiceHandle\s*\(" },
    @{ Category = "Registry Keys"; Alloc = "\b(RegOpenKeyExW|RegCreateKeyExW)\s*\("; Free = "\bRegCloseKey\s*\(" },
    @{ Category = "GDI Objects"; Alloc = "\b(CreateFontIndirectW|CreateFontW|CreateSolidBrush|CreatePen|CreateCompatibleBitmap)\s*\("; Free = "\bDeleteObject\s*\(" },
    @{ Category = "Icons"; Alloc = "\b(ExtractIconExW|SHGetFileInfoW|LoadIconW|CreateIconIndirect)\s*\("; Free = "\bDestroyIcon\s*\(" },
    @{ Category = "Shell PIDLs"; Alloc = "\b(ILCreateFromPathW|SHSimpleIDListFromPath)\s*\("; Free = "\b(ILFree|CoTaskMemFree)\s*\(" },
    @{ Category = "Heap new/delete"; Alloc = "\bnew\s+[A-Za-z0-9_]+"; Free = "\b(delete\s+|std::unique_ptr|std::make_unique|std::shared_ptr|std::make_shared)" }
)

foreach ($pair in $ResourcePairs) {
    $allocMatches = Get-PatternMatches -Pattern $pair.Alloc
    $freeMatches  = Get-PatternMatches -Pattern $pair.Free
    Write-Host ("  - {0,-18}: Alloc Sites = {1,3} | Cleanup Sites = {2,3}" -f $pair.Category, $allocMatches.Count, $freeMatches.Count)
}

# 2. High-Risk Pattern Hotspots
Write-Host "`n[2/4] High-Risk Vulnerability & Bug Pattern Hotspots" -ForegroundColor Yellow

$HotspotChecks = @(
    @{ Name = "Self-deletion & Window Destruction (WM_DESTROY / WM_NCDESTROY / delete this)"; Pattern = "(WM_DESTROY|WM_NCDESTROY|delete\s+this)" },
    @{ Name = "Subprocess / Shell Execution (ShellExecute / CreateProcess)"; Pattern = "\b(ShellExecuteW|ShellExecuteExW|CreateProcessW|WinExec)\b" },
    @{ Name = "Async PostMessage with Pointer Payload"; Pattern = "\bPostMessageW\s*\(" },
    @{ Name = "Unbounded Map / Cache Candidates (unordered_map / map in headers)"; Pattern = "std::(unordered_)?map<" },
    @{ Name = "Process Termination & Priority Operations"; Pattern = "\b(TerminateProcess|SetPriorityClass|ControlService|StartServiceW)\b" },
    @{ Name = "Forced Process Exit / Build Kill"; Pattern = "\b(ExitProcess|Stop-Process)\b" }
)

foreach ($check in $HotspotChecks) {
    $hits = Get-PatternMatches -Pattern $check.Pattern
    Write-Host "`n  * $($check.Name) ($($hits.Count) matches):" -ForegroundColor Green
    foreach ($hit in $hits) {
        $relPath = $hit.Path.Substring($RepoRoot.Path.Length + 1)
        Write-Host ("    {0}:{1}: {2}" -f $relPath, $hit.LineNumber, $hit.Line.Trim())
    }
}

# 3. Binary Hardening & Build Verification
Write-Host "`n[3/4] Build & Binary Mitigation Check ($Configuration | x64)" -ForegroundColor Yellow

$VsDevCmdCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"
)
$VsDevCmd = $VsDevCmdCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $SkipBuild -and $VsDevCmd) {
    Write-Host "  Building LiteProcManager.sln [$Configuration|x64] with /W4 check..."
    $buildCmd = "`"$VsDevCmd`" -arch=x64 >nul 2>&1 && msbuild `"$RepoRoot\LiteProcManager.sln`" /p:Configuration=$Configuration /p:Platform=x64 /nologo /verbosity:minimal /m"
    cmd.exe /c $buildCmd
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [WARN] MSBuild returned exit code $LASTEXITCODE" -ForegroundColor Red
    } else {
        Write-Host "  [OK] MSBuild succeeded." -ForegroundColor Green
    }
}

$ExePath = Join-Path $RepoRoot "bin\$Configuration\LiteProcManager.exe"
if (Test-Path $ExePath) {
    Write-Host "  Inspecting binary mitigations on: $ExePath"
    if ($VsDevCmd) {
        $dumpCmd = "`"$VsDevCmd`" -arch=x64 >nul 2>&1 && dumpbin /headers `"$ExePath`" | findstr /I `"Dynamic High NX Guard`""
        $headersOut = cmd.exe /c $dumpCmd
        foreach ($line in $headersOut) {
            Write-Host "    $line"
        }
    }
    $sig = Get-AuthenticodeSignature -FilePath $ExePath
    Write-Host "    Authenticode Signature Status: $($sig.Status)"
} else {
    Write-Host "  [INFO] Binary not found at $ExePath"
}

# 4. Unit Test Execution
Write-Host "`n[4/4] Unit Test Verification" -ForegroundColor Yellow
$TestDll = Join-Path $RepoRoot "bin\$Configuration\LiteProcManagerTests.dll"
if (-not $SkipTests -and (Test-Path $TestDll) -and $VsDevCmd) {
    $testCmd = "`"$VsDevCmd`" -arch=x64 >nul 2>&1 && vstest.console.exe `"$TestDll`" --logger:`"Console;verbosity=minimal`""
    cmd.exe /c $testCmd
} elseif ($SkipTests) {
    Write-Host "  [SKIP] Unit tests skipped via -SkipTests."
} else {
    Write-Host "  [WARN] Test DLL not found at $TestDll or Visual Studio Developer tools unavailable."
}

Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host " Automated Scan Complete. Review checklist.md for deep audit." -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
