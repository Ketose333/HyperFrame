$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = "0.1.0"
$buildPlugin = Join-Path $root "build-juce\HyperFrame_artefacts\Release\VST3\HyperFrame.vst3"
$packageRoot = Join-Path $root "build\release"
$stage = Join-Path $packageRoot "HyperFrame-$version-win-vst3"
$zip = Join-Path $packageRoot "HyperFrame-$version-win-vst3.zip"
$packageRootFull = [System.IO.Path]::GetFullPath($packageRoot)
$stageFull = [System.IO.Path]::GetFullPath($stage)
$zipFull = [System.IO.Path]::GetFullPath($zip)

if (!$stageFull.StartsWith($packageRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to stage outside package root: $stageFull"
}

if (!$zipFull.StartsWith($packageRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to write zip outside package root: $zipFull"
}

if (!(Test-Path $buildPlugin)) {
    throw "Expected VST3 was not found at $buildPlugin. Run scripts\build_vst3.ps1 first."
}

if (Test-Path $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $buildPlugin -Destination $stage -Recurse -Force

foreach ($file in @("README.md", "LICENSE", "RELEASE_NOTES.md", "THIRD_PARTY_NOTICES.md")) {
    $source = Join-Path $root $file
    if (Test-Path $source) {
        Copy-Item -LiteralPath $source -Destination $stage -Force
    }
}

if (Test-Path $zip) {
    Remove-Item -LiteralPath $zip -Force
}

Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$dll = Join-Path $stage "HyperFrame.vst3\Contents\x86_64-win\HyperFrame.vst3"
$hash = Get-FileHash -Algorithm SHA256 $dll
Write-Host "Packaged: $zip"
Write-Host "SHA256: $($hash.Hash)"
