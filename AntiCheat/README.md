# AntiCheat 1.0.0

![AntiCheat logo](../docs/AntiCheat-logo.svg)

AntiCheat 1.0.0 is maintained by GSM-RO. Existing copyright notices and licenses for inherited and third-party code remain in force.

## Linux x86-64 build

The Linux build uses Steam Runtime 3 SDK image `registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce` and AMBuild commit `d89ec91a7ac2607da07b50bb62346f9a10e9a998`. A Windows host needs Docker Desktop with its Linux engine, Git, Python 3, the prepared CS2 SDK, and pinned AMBuild checkout. Linux hosts need Docker, Git, Python 3, and the same prepared source/build inputs.

Use an isolated MetaMod:Source checkout at **2.0.0.1483**, commit `170058a2f0dbd58106b7c76bfe88a94672295356`, with its recursive submodules initialized. AlliedModders lists Counter-Strike 2 as requiring current-development MetaMod:Source; confirm the target against the [official required-version table](https://wiki.alliedmods.net/Required_Versions_(SourceMod)) and [development snapshots](https://www.metamodsource.net/downloads.php/?branch=dev).

From the repository root in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-linux.ps1 `
  -BuildDirectory 'AntiCheat/build-linux-release' `
  -MetaModSourceDirectory 'C:\tools\metamod-source-2.0.0.1483' `
  -SkipBootstrap
```

On Linux, use the equivalent wrapper:

```sh
./build-linux.sh AntiCheat/build-linux-release /opt/metamod-source-2.0.0.1483 --skip-bootstrap
```

The output package is under `AntiCheat/<build-directory>/package`. The plugin binary is `AntiCheat.so` at `game/csgo/addons/AntiCheat/bin/linuxsteamrt64/AntiCheat.so`. The MetaMod loader alias is `AntiCheat`, and the configuration and gamedata files use the same project name. The package includes `COPYRIGHT.md`, `THIRD_PARTY_NOTICES.md`, and applicable license files under `game/csgo/addons/AntiCheat/licenses`.

## Copyright and licensing

Copyright (c) 2026 GSM-RO for original AntiCheat modifications contributed for this release. This attribution does not claim ownership of inherited code or third-party components; their copyright notices and licenses remain applicable.

The repository currently has a licensing mismatch: `plugin-metadata.json` identifies AGPL-3.0, while the root `LICENSE` file contains GNU GPL version 3 text. This rebrand preserves both existing materials and does not resolve or change the project's governing license. Confirm the correct project license with the relevant rights holders before redistributing.

Choose a fresh build-directory name if that output already exists. The separate `package-source.py` source-archive operation requires a clean tracked tree and valid pinned dependency checkouts; it is not required for the Linux runtime package build.
