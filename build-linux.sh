#!/usr/bin/env bash
set -euo pipefail

readonly BUILD_DIR="${1:-AntiCheat/build-linux-release}"
readonly SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
METAMOD_SOURCE_INPUT="${2:-$SOURCE_DIR/metamod-source}"
readonly SKIP_BOOTSTRAP="${3:-}"
readonly IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce"
readonly REQUIRED_METAMOD_COMMIT="ea513d8960c52d266a1c5620dc762def0dd8bce8"
readonly REQUIRED_AMBUILD_COMMIT="d89ec91a7ac2607da07b50bb62346f9a10e9a998"

[[ "$BUILD_DIR" =~ ^[A-Za-z0-9_-]+(/[A-Za-z0-9_-]+)*$ ]] ||
  { echo "BUILD_DIR must be a safe repository-relative path using letters, numbers, underscores, dashes, and forward slashes." >&2; exit 1; }
command -v docker >/dev/null ||
  { echo "Docker is required for the Linux Steam Runtime build." >&2; exit 1; }

if [[ "$METAMOD_SOURCE_INPUT" != /* ]]; then
  METAMOD_SOURCE_INPUT="$SOURCE_DIR/$METAMOD_SOURCE_INPUT"
fi
[[ -d "$METAMOD_SOURCE_INPUT/core" ]] ||
  { echo "MetaMod:Source checkout is missing or invalid at $METAMOD_SOURCE_INPUT." >&2; exit 1; }
METAMOD_SOURCE_DIR="$(cd -- "$METAMOD_SOURCE_INPUT" && pwd)"
command -v git >/dev/null ||
  { echo "Git is required to verify the pinned MetaMod:Source revision." >&2; exit 1; }
METAMOD_REVISION="$(git -C "$METAMOD_SOURCE_DIR" rev-parse HEAD 2>/dev/null)" ||
  { echo "Could not read the MetaMod:Source Git revision at $METAMOD_SOURCE_DIR." >&2; exit 1; }
[[ "$METAMOD_REVISION" == "$REQUIRED_METAMOD_COMMIT" ]] ||
  { echo "MetaMod:Source must be checked out at $REQUIRED_METAMOD_COMMIT (found $METAMOD_REVISION). Use an isolated checkout." >&2; exit 1; }
if [[ "$SKIP_BOOTSTRAP" == "--skip-bootstrap" ]]; then
  [[ -d "$SOURCE_DIR/hl2sdk-cs2/public" && -d "$SOURCE_DIR/.tools/ambuild/ambuild2" ]] ||
    { echo "The CS2 SDK or pinned AMBuild is missing; run bootstrap.sh or provide prepared inputs." >&2; exit 1; }
  AMBUILD_REVISION="$(git -C "$SOURCE_DIR/.tools/ambuild" rev-parse HEAD 2>/dev/null)" ||
    { echo "Could not read the pinned AMBuild Git revision." >&2; exit 1; }
  [[ "$AMBUILD_REVISION" == "$REQUIRED_AMBUILD_COMMIT" ]] ||
    { echo "AMBuild must be checked out at $REQUIRED_AMBUILD_COMMIT (found $AMBUILD_REVISION)." >&2; exit 1; }
else
  "$SOURCE_DIR/bootstrap.sh"
fi
docker version --format '{{.Server.Version}}' >/dev/null

docker run --rm \
  -v "$SOURCE_DIR:/workspace" \
  -v "$METAMOD_SOURCE_DIR:/metamod-source:ro" \
  -v "$SOURCE_DIR/.tools/ambuild:/opt/ambuild:ro" \
  -w /workspace \
  "$IMAGE" bash -lc "
    set -eu
    export DEBIAN_FRONTEND=noninteractive PYTHONPATH=/opt/ambuild
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends lld >/dev/null
    python3 configure.py --enable-optimize --metamod-source /metamod-source --out '$BUILD_DIR'
    python3 -c 'from ambuild2.run import cli_run; cli_run()' '$BUILD_DIR'
  "

plugin="$SOURCE_DIR/$BUILD_DIR/package/game/csgo/addons/AntiCheat/bin/linuxsteamrt64/AntiCheat.so"
[[ -f "$plugin" ]] || { echo "The build finished without producing AntiCheat.so." >&2; exit 1; }
echo "Linux package ready: $plugin"
