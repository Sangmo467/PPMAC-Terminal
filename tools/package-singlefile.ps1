# Package an existing Release deployment as one Windows x64 executable.
[CmdletBinding()]
param(
    [string]$SourceDir = (Join-Path $PSScriptRoot '../dist'),
    [string]$OutputFile = (Join-Path $PSScriptRoot '../dist_single/PmacTerminalApp.exe'),
    [string]$EnigmaConsole = (Join-Path ${env:ProgramFiles(x86)} 'Enigma Virtual Box/enigmavbconsole.exe'),
    [string]$CrtDir
)
$ErrorActionPreference = 'Stop'
$SourceDir = [IO.Path]::GetFullPath($SourceDir)
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$inputFile = Join-Path $SourceDir 'PmacTerminalApp.exe'
foreach ($required in @($inputFile, (Join-Path $SourceDir 'pmac.dll'),
        (Join-Path $SourceDir 'pmac_terminal_core.dll'),
        (Join-Path $SourceDir 'platforms/qwindows.dll'), $EnigmaConsole)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file not found: $required. Build Release first and install Enigma Virtual Box."
    }
}
if ($OutputFile.StartsWith($SourceDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputFile must be outside SourceDir.'
}
if (-not $CrtDir) {
    $candidates = Get-ChildItem "${env:ProgramFiles}/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT" -Directory -ErrorAction SilentlyContinue
    $CrtDir = $candidates | Sort-Object { [version]$_.Parent.Parent.Name } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $CrtDir -or -not (Test-Path -LiteralPath (Join-Path $CrtDir 'vcruntime140.dll'))) {
    throw 'MSVC x64 redistributable DLLs not found. Supply -CrtDir <Microsoft.VC143.CRT directory>.'
}
$projectDir = Join-Path $PSScriptRoot '../build/singlefile'
$projectDir = [IO.Path]::GetFullPath($projectDir)
$null = New-Item -ItemType Directory -Path $projectDir -Force
$null = New-Item -ItemType Directory -Path (Split-Path $OutputFile -Parent) -Force
$projectFile = Join-Path $projectDir 'PmacTerminalApp.evb'
$xml = [Text.StringBuilder]::new()
function Add-Tag([string]$Name, [string]$Value) {
    $escaped = [Security.SecurityElement]::Escape($Value)
    $null = $xml.AppendLine("<$Name>$escaped</$Name>")
}
function Add-VirtualEntry([IO.FileSystemInfo]$Item) {
    if ($Item.FullName -eq $inputFile) { return }
    if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Unexpected link in deployment: $($Item.FullName)"
    }
    $null = $xml.AppendLine('<File>')
    Add-Tag 'Type' $(if ($Item.PSIsContainer) { '3' } else { '2' })
    Add-Tag 'Name' $Item.Name
    if (-not $Item.PSIsContainer) {
        Add-Tag 'File' $Item.FullName
        Add-Tag 'ActiveX' 'false'
        Add-Tag 'ActiveXInstall' 'false'
    }
    Add-Tag 'Action' '0'
    Add-Tag 'OverwriteDateTime' 'false'
    Add-Tag 'OverwriteAttributes' 'false'
    if ($Item.PSIsContainer) {
        $null = $xml.AppendLine('<Files>')
        foreach ($child in (Get-ChildItem -LiteralPath $Item.FullName | Sort-Object Name)) {
            Add-VirtualEntry $child
        }
        $null = $xml.AppendLine('</Files>')
    } else {
        Add-Tag 'PassCommandLine' 'false'
    }
    $null = $xml.AppendLine('</File>')
}
$null = $xml.AppendLine('<?xml version="1.0" encoding="utf-8"?>')
$null = $xml.AppendLine('<Project>')
Add-Tag 'InputFile' $inputFile
Add-Tag 'OutputFile' $OutputFile
$null = $xml.AppendLine('<Files>')
Add-Tag 'Enabled' 'true'
Add-Tag 'DeleteExtractedOnExit' 'true'
Add-Tag 'CompressFiles' 'true'
$null = $xml.AppendLine('<Files><File>')
Add-Tag 'Type' '3'
Add-Tag 'Name' '%DEFAULT FOLDER%'
Add-Tag 'Action' '0'
Add-Tag 'OverwriteDateTime' 'false'
Add-Tag 'OverwriteAttributes' 'false'
$null = $xml.AppendLine('<Files>')
foreach ($entry in (Get-ChildItem -LiteralPath $SourceDir | Sort-Object Name)) {
    Add-VirtualEntry $entry
}
foreach ($runtime in (Get-ChildItem -LiteralPath $CrtDir -Filter '*.dll' | Sort-Object Name)) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceDir $runtime.Name))) {
        Add-VirtualEntry $runtime
    }
}
$null = $xml.AppendLine('</Files></File></Files></Files>')
$null = $xml.AppendLine('<Registries><Enabled>false</Enabled><Registries /></Registries>')
$null = $xml.AppendLine('<Packaging><Enabled>false</Enabled></Packaging>')
$null = $xml.AppendLine('<Options>')
Add-Tag 'ShareVirtualSystem' 'false'
Add-Tag 'MapExecutableWithTemporaryFile' 'true'
Add-Tag 'AllowRunningOfVirtualExeFiles' 'true'
$null = $xml.AppendLine('</Options></Project>')
[IO.File]::WriteAllText($projectFile, $xml.ToString(), [Text.UTF8Encoding]::new($false))
& $EnigmaConsole $projectFile
if ($LASTEXITCODE -ne 0) { throw "Enigma Virtual Box failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $OutputFile -PathType Leaf)) { throw 'Single-file output was not created.' }
$result = Get-Item -LiteralPath $OutputFile
if ($result.Length -le (Get-Item -LiteralPath $inputFile).Length) { throw 'Single-file output is unexpectedly small.' }
Write-Output ('Single-file application: {0} ({1:N1} MB)' -f $result.FullName, ($result.Length / 1MB))
