param(
	[string]$BuildDirectory = 'AntiCheat/build-linux-release',
	[string]$MetaModSourceDirectory = 'metamod-source',
	[switch]$SkipBootstrap
)

$ErrorActionPreference = 'Stop'
$requiredMetaModCommit = 'ea513d8960c52d266a1c5620dc762def0dd8bce8'
$requiredAMBuildCommit = 'd89ec91a7ac2607da07b50bb62346f9a10e9a998'

if ($BuildDirectory -notmatch '^[A-Za-z0-9_-]+(/[A-Za-z0-9_-]+)*$') {
	throw 'BuildDirectory must be a safe repository-relative path using letters, numbers, underscores, dashes, and forward slashes.'
}
if (-not (Get-Command docker.exe -ErrorAction SilentlyContinue)) {
	throw 'Docker Desktop is required for the Linux build.'
}

$buildPath = Join-Path $PSScriptRoot ($BuildDirectory.Replace('/', '\'))
$metaModSourcePath = $MetaModSourceDirectory
if (-not [System.IO.Path]::IsPathRooted($metaModSourcePath)) {
	$metaModSourcePath = Join-Path $PSScriptRoot $metaModSourcePath
}
$metaModSourcePath = [System.IO.Path]::GetFullPath($metaModSourcePath)
$amBuildPath = Join-Path $PSScriptRoot '.tools\ambuild'
if (-not (Test-Path (Join-Path $metaModSourcePath 'core'))) {
	throw "MetaMod:Source checkout is missing or invalid at $metaModSourcePath."
}
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) {
	throw 'Git is required to verify the pinned MetaMod:Source revision.'
}
$metaModRevision = (& $git.Source -C $metaModSourcePath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $metaModRevision -ne $requiredMetaModCommit) {
	throw "MetaMod:Source must be checked out at $requiredMetaModCommit (found $metaModRevision). Use an isolated checkout; do not update a pre-existing dependency directory."
}
if (-not $SkipBootstrap) {
	& (Join-Path $PSScriptRoot 'bootstrap.ps1')
}
else {
	if (-not (Test-Path (Join-Path $PSScriptRoot 'hl2sdk-cs2\public'))) {
		throw 'The CS2 SDK is missing; run bootstrap.ps1 or provide a prepared source checkout.'
	}
	if (-not (Test-Path (Join-Path $PSScriptRoot '.tools\ambuild\ambuild2'))) {
		throw 'Pinned AMBuild is missing; run bootstrap.ps1 or provide a prepared source checkout.'
	}
	$amBuildRevision = (& $git.Source -C $amBuildPath rev-parse HEAD).Trim()
	if ($LASTEXITCODE -ne 0 -or $amBuildRevision -ne $requiredAMBuildCommit) {
		throw "Pinned AMBuild must be checked out at $requiredAMBuildCommit (found $amBuildRevision)."
	}
}

$image = 'registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce'
$docker = (Get-Command docker.exe).Source
$sourcePath = $PSScriptRoot
& $docker version --format '{{.Server.Version}}' | Out-Null
if ($LASTEXITCODE -ne 0) {
	throw 'Docker Desktop is not running with its Linux container engine.'
}

$build = ('set -eu; export DEBIAN_FRONTEND=noninteractive; apt-get update >/dev/null; ' +
	'apt-get install -y --no-install-recommends lld >/dev/null; export PYTHONPATH=/opt/ambuild; ' +
	"python3 configure.py --enable-optimize --metamod-source /metamod-source --out $BuildDirectory; " +
	"python3 -c 'from ambuild2.run import cli_run; cli_run()' $BuildDirectory")

& $docker run --rm `
	--mount "type=bind,source=$sourcePath,target=/workspace" `
	--mount "type=bind,source=$metaModSourcePath,target=/metamod-source,readonly" `
	--mount "type=bind,source=$amBuildPath,target=/opt/ambuild,readonly" `
	--workdir /workspace $image bash -lc $build
if ($LASTEXITCODE -ne 0) {
	throw 'The Linux SteamRT3 build failed.'
}

$plugin = Join-Path $buildPath 'package\game\csgo\addons\AntiCheat\bin\linuxsteamrt64\AntiCheat.so'
if (-not (Test-Path $plugin)) {
	throw 'The build finished without producing AntiCheat.so.'
}
Write-Host "Linux package ready: $plugin"
