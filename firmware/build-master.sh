#!/usr/bin/env bash
# Build Kinetos OpenEVSE V5 on current OpenEVSE master (rev 9+):
#   upstream master (pinned) + Kinetos board / SDM meter (PR1) + grid dimming §14a (PR2)
#   + Modbus TCP / smart1 (PR3) + Kinetos extras (LED PV share, HA discovery, panic trap,
#   settings migration from rev <= 8) + Kinetos page in gui-nightshift (English only)
#   + EEBUS (openeebus by NIBE; patches-master/*-0002-eebus.patch)
#   + meter detection SDM630/DTS4921, 8N1/8E1 (patches-master/firmware-0003-meter-detect.patch)
#   + controller meter feed retries $F2 0 until accepted, rounded meter values
#     (patches-master/firmware-0004-feed-retry-rounding.patch, rev 21)
#   + controller version "E532_L-8.2.0" accepted for the meter feed
#     (patches-master/firmware-0005-controller-version.patch, rev 22).
#
# Two images come out of the same sources, because OCPP and EEBUS do not both fit into
# the 4 MB flash:
#   out/kinetos-v5-<version>.bin        with OCPP
#   out/kinetos-eebus-v5-<version>.bin  with EEBUS instead of OCPP
# Release assets must start with the buildenv: the update check of the wallbox picks its
# asset by that prefix. Flash via the wallbox's /update page.
#
# Needs git, node/npm, patch, python3 and PlatformIO (or uv, which fetches it).
set -euo pipefail
UPSTREAM="${UPSTREAM:-a3816295}"
VERSION="${VERSION:-v5.2.0-dev-kinetos.22}"
ENVS="${ENVS:-kinetos-v5 kinetos-eebus-v5}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$HERE/.work-master}"
GIT=(git -c user.name=build -c user.email=build@localhost)
if [ ! -d "$WORK/.git" ]; then
  git clone -q https://github.com/OpenEVSE/ESP32_WiFi_V4.x.git "$WORK"
fi
cd "$WORK"
git fetch -q origin
git checkout -q -f --detach "$UPSTREAM"
git clean -qfdx src
git submodule update -q --init --recursive
git submodule foreach -q --recursive 'git checkout -q -- . && git clean -qfd'

# GUI: Kinetos settings pages, English only
for patch in "$HERE"/patches-master/gui-*.patch; do
  git -C gui-nightshift apply "$patch"
done
(cd gui-nightshift && npm ci --no-audit --no-fund --silent && npm run build --silent)
"${GIT[@]}" -C gui-nightshift add -A && "${GIT[@]}" -C gui-nightshift commit -qm "kinetos gui"
git add gui-nightshift

# Firmware
for patch in "$HERE"/patches-master/firmware-*.patch; do
  git apply "$patch"
done
"${GIT[@]}" add -A && "${GIT[@]}" commit -qm "kinetos"

export GITHUB_REF_NAME="$VERSION"
if command -v pio >/dev/null; then PIO=(pio); else PIO=(uv tool run --from platformio pio); fi
# First pass regenerates the GUI headers into src/, commit them so the version is not "_modified"
"${PIO[@]}" run -e "${ENVS%% *}"
"${GIT[@]}" add -A src && "${GIT[@]}" commit -qm "generated headers" --allow-empty
mkdir -p "$HERE/out"
for env in $ENVS; do
  # A build rewrites the generated headers, with the same content. Refresh the index so
  # the next build does not take them for changes and append "_modified" to the version.
  git update-index -q --refresh || true
  "${PIO[@]}" run -e "$env"
  cp ".pio/build/$env/firmware.bin" "$HERE/out/$env-$VERSION.bin"
  cp ".pio/build/$env/firmware.elf" "$HERE/out/$env-$VERSION.elf"
done
for env in $ENVS; do
  if command -v sha256sum >/dev/null; then sha256sum "$HERE/out/$env-$VERSION.bin"; else shasum -a 256 "$HERE/out/$env-$VERSION.bin"; fi
done
