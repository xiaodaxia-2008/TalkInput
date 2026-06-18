# FiberArt MSVC wrapper script
#
# Usage:
#   pwsh.exe -File msvc.ps1 <command> [args...]    -- activate MSVC then run command
#   . msvc.ps1; Activate-Msvc                      -- dot-source to get Activate-Msvc function
#
# Examples:
#   To configure using the release/relwithdebinfo preset:
#     pwsh -File msvc.ps1 cmake --preset release/relwithdebinfo
#
#   To build using the configured build directory:
#     pwsh -File msvc.ps1 cmake --build build

function Get-VsInstallRoot {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs_path = & $vswhere -latest -products * -property installationPath 2>$null
        if ($vs_path) { return $vs_path }
    }

    $default = "C:\Program Files\Microsoft Visual Studio\2022\Professional"
    if (Test-Path $default) { return $default }
    $default = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    if (Test-Path $default) { return $default }
    $default = "C:\Program Files\Microsoft Visual Studio\2022\Community"
    if (Test-Path $default) { return $default }

    return $null
}

function Activate-Msvc {
    $orig_dir = $PWD
    $vs_root = Get-VsInstallRoot

    if (-not $vs_root) {
        Write-Error "[activate-msvc] Cannot find Visual Studio 2022 installation."
        return $false
    }

    $vsdevshell = Join-Path $vs_root "Common7\Tools\Launch-VsDevShell.ps1"

    if (Test-Path $vsdevshell) {
        & $vsdevshell -Arch amd64
        Write-Output "[activate-msvc] MSVC environment activated via Launch-VsDevShell.ps1"
    } else {
        $vcvarsall = Join-Path $vs_root "VC\Auxiliary\Build\vcvarsall.bat"
        if (Test-Path $vcvarsall) {
            cmd /c "call `"$vcvarsall`" x64 >nul 2>&1 && set" | ForEach-Object {
                if ($_ -match "^(.*?)=(.*)$") {
                    Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
                }
            }
            Write-Output "[activate-msvc] MSVC environment activated via vcvarsall.bat"
        } else {
            Write-Error "[activate-msvc] Cannot find VS dev shell scripts."
            return $false
        }
    }

    Set-Location $orig_dir
    return $true
}

if ($args.Count -gt 0) {
    if (-not (Activate-Msvc)) { exit 1 }

    if ($args.Count -eq 1) {
        & $args[0]
    } else {
        & $args[0] @($args[1..($args.Count - 1)])
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}