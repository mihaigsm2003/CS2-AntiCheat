# AntiCheat 1.0.1

![AntiCheat logo](../docs/AntiCheat-logo.svg)

AntiCheat 1.0.1 is maintained by GSM-RO. Existing copyright notices and licenses for inherited and third-party code remain in force.

Version 1.0.1 fixes Windows and Linux linking against the CS2 SDK's KeyValues3 and EntityKeyValues implementations and uses the SDK's GameEntitySystem accessor instead of a stale gamedata offset.

## Linux x86-64 build

The Linux build uses Steam Runtime 3 SDK image `registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce` and AMBuild commit `d89ec91a7ac2607da07b50bb62346f9a10e9a998`. A Windows host needs Docker Desktop with its Linux engine, Git, Python 3, the prepared CS2 SDK, and pinned AMBuild checkout. Linux hosts need Docker, Git, Python 3, and the same prepared source/build inputs.

Use an isolated MetaMod:Source checkout at **2.0.0.1490**, commit `ea513d8960c52d266a1c5620dc762def0dd8bce8`, with its recursive submodules initialized. The CS2 SDK is the `cs2` branch of [alliedmodders/s2sdk](https://github.com/alliedmodders/s2sdk), pinned in CI at `a508eb5ed5fb36f4067fa36a1c33f5fec7d73066`. AlliedModders lists Counter-Strike 2 as requiring current-development MetaMod:Source; confirm the target against the [official required-version table](https://wiki.alliedmods.net/Required_Versions_(SourceMod)) and [development snapshots](https://www.metamodsource.net/downloads.php/?branch=dev).

AntiCheat is a native MetaMod plugin, not a CounterStrikeSharp extension. It does not depend on CounterStrikeSharp APIs and can be loaded alongside CounterStrikeSharp **v1.0.380**, which supports CS2 update 1.41.9.7. Both plugins must use the current CS2-compatible MetaMod:Source runtime.

From the repository root in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-linux.ps1 `
  -BuildDirectory 'AntiCheat/build-linux-release' `
  -MetaModSourceDirectory 'C:\tools\metamod-source-2.0.0.1490' `
  -SkipBootstrap
```

On Linux, use the equivalent wrapper:

```sh
./build-linux.sh AntiCheat/build-linux-release /opt/metamod-source-2.0.0.1490 --skip-bootstrap
```

The output package is under `AntiCheat/<build-directory>/package`. The plugin binary is `AntiCheat.so` at `game/csgo/addons/AntiCheat/bin/linuxsteamrt64/AntiCheat.so`. The MetaMod loader alias is `AntiCheat`, and the configuration and gamedata files use the same project name. The package includes `COPYRIGHT.md`, `THIRD_PARTY_NOTICES.md`, and applicable license files under `game/csgo/addons/AntiCheat/licenses`.

## Copyright and licensing

Copyright (c) 2026 GSM-RO for original AntiCheat modifications contributed for this release. This attribution does not claim ownership of inherited code or third-party components; their copyright notices and licenses remain applicable.

The repository currently has a licensing mismatch: `plugin-metadata.json` identifies AGPL-3.0, while the root `LICENSE` file contains GNU GPL version 3 text. This rebrand preserves both existing materials and does not resolve or change the project's governing license. Confirm the correct project license with the relevant rights holders before redistributing.

Choose a fresh build-directory name if that output already exists. The separate `package-source.py` source-archive operation requires a clean tracked tree and valid pinned dependency checkouts; it is not required for the Linux runtime package build.
