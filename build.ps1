<#
  Builds the plugin (32 + 64-bit) and the .rmskin installer (in dist\).

  Needs llvm-mingw: https://github.com/mstorsjo/llvm-mingw/releases (ucrt-x86_64 zip)
  Pass its folder with -Toolchain, or put its bin on PATH.

  Usage:
    powershell -ExecutionPolicy Bypass -File build.ps1 -Version 1.0.0 -Toolchain C:\llvm-mingw

  skin\@Resources\Variables.inc = defaults for new users (no devices set).
  On upgrade, the installer keeps each user's values (VariableFiles).
#>
param(
	[string]$Version = '1.0.0',
	[string]$Toolchain = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$Name = 'Audio + Mic Switch'
$Author = 'AdviceWithSalt & ok-jaime'
$Plugin = 'AudioMicSwitch'
$SkinPath = Join-Path $PSScriptRoot 'skin'
$obj = Join-Path $PSScriptRoot 'obj'
$dist = Join-Path $PSScriptRoot 'dist'
Remove-Item $obj -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $obj, $dist | Out-Null

function Tool([string]$name)
{
	if ($Toolchain) { return Join-Path $Toolchain "bin\$name.exe" }
	return "$name.exe"
}

# --- Plugin -----------------------------------------------------------------
$numbers = (($Version.Split('.') | ForEach-Object { [int]$_ }) + @(0, 0, 0, 0))[0..3]
$rcFile = Join-Path $obj "$Plugin.rc"
@"
#include <winver.h>
1 VERSIONINFO
FILEVERSION $($numbers -join ',')
PRODUCTVERSION $($numbers -join ',')
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "FileDescription", "$Name plugin for Rainmeter"
      VALUE "FileVersion", "$($numbers -join '.')"
      VALUE "LegalCopyright", "$Author"
      VALUE "OriginalFilename", "$Plugin.dll"
      VALUE "ProductName", "$Name"
      VALUE "ProductVersion", "$($numbers -join '.')"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@ | Set-Content -Encoding ASCII $rcFile

$stage = Join-Path $obj 'package'
foreach ($arch in @(@{ Folder = '64bit'; Target = 'x86_64' }, @{ Folder = '32bit'; Target = 'i686' }))
{
	$out = Join-Path $stage "Plugins\$($arch.Folder)"
	New-Item -ItemType Directory -Force $out | Out-Null
	$res = Join-Path $obj "$Plugin-$($arch.Folder).res.o"
	& (Tool "$($arch.Target)-w64-mingw32-windres") $rcFile -O coff -o $res
	if ($LASTEXITCODE) { throw "windres failed ($($arch.Folder))" }
	# No link timestamp, so the same source and toolchain always give the same DLL.
	& (Tool "$($arch.Target)-w64-mingw32-clang++") -std=c++17 -O2 -Wall -shared -static -s '-Wl,--no-insert-timestamp' `
		-o (Join-Path $out "$Plugin.dll") (Join-Path $PSScriptRoot "plugin\$Plugin.cpp") $res -lole32
	if ($LASTEXITCODE) { throw "compile failed ($($arch.Folder))" }
	Write-Host "Built $($arch.Folder)\$Plugin.dll"
}

# --- Skin -------------------------------------------------------------------
$skinStage = Join-Path $stage "Skins\$Name"
Get-ChildItem $SkinPath -Recurse -File -Include *.ini, *.inc, *.png | ForEach-Object {
	$target = Join-Path $skinStage $_.FullName.Substring($SkinPath.Length + 1)
	New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
	Copy-Item $_.FullName $target
}

@"
[rmskin]
Name=$Name
Author=$Author
Version=$Version
LoadType=Skin
Load=$Name\$Name.ini
VariableFiles=$Name\@Resources\Variables.inc
MinimumRainmeter=4.1.0
MinimumWindows=10.0
"@ | Set-Content -Encoding ASCII (Join-Path $stage 'RMSKIN.ini')

# --- .rmskin: a zip (RMSKIN.ini first) followed by a 16-byte footer:
#     zip size (int64), flags (byte, 0), "RMSKIN\0"
$package = Join-Path $dist "$($Plugin)_$Version.rmskin"
$stream = [IO.File]::Open($package, [IO.FileMode]::Create)
$zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
$files = @(Get-Item (Join-Path $stage 'RMSKIN.ini')) + @(Get-ChildItem $stage -Recurse -File | Where-Object Name -ne 'RMSKIN.ini')
foreach ($file in $files)
{
	$entry = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
	[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
$zip.Dispose()
$zipSize = $stream.Length
$writer = New-Object IO.BinaryWriter($stream)
$writer.Write([int64]$zipSize)
$writer.Write([byte]0)
$writer.Write([Text.Encoding]::ASCII.GetBytes("RMSKIN`0"))
$writer.Dispose()

Write-Host "Packaged $package"
