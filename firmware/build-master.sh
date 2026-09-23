#!/usr/bin/env bash
# Build Kinetos OpenEVSE V5 on current OpenEVSE master (rev 9+):
#   upstream master (pinned) + Kinetos board / SDM meter (PR1) + grid dimming §14a (PR2)
#   + Modbus TCP / smart1 (PR3) + Kinetos extras (LED PV share, HA discovery, panic trap,
#   settings migration from rev <= 8) + Kinetos page in gui-nightshift (English only).
# Output: out/kinetos-openevse-<version>.bin (flash via the wallbox's /update page)
set -euo pipefail
UPSTREAM="${UPSTREAM:-a3816295}"
VERSION="${VERSION:-v5.2.0-dev-kinetos.12}"
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

# GUI: Kinetos settings page, English only
git -C gui-nightshift apply "$HERE"/patches-master/gui-*.patch
(cd gui-nightshift && npm ci --no-audit --no-fund --silent && npm run build --silent)
"${GIT[@]}" -C gui-nightshift add -A && "${GIT[@]}" -C gui-nightshift commit -qm "kinetos gui"
git add gui-nightshift

# Firmware
git apply "$HERE"/patches-master/firmware-*.patch
"${GIT[@]}" add -A && "${GIT[@]}" commit -qm "kinetos"

export GITHUB_REF_NAME="$VERSION"
if command -v pio >/dev/null; then PIO=(pio); else PIO=(uv tool run --from platformio pio); fi
# First pass regenerates the GUI headers into src/, commit them so the version is not "_modified"
"${PIO[@]}" run -e kinetos-v5
"${GIT[@]}" add -A src && "${GIT[@]}" commit -qm "generated headers" --allow-empty
"${PIO[@]}" run -e kinetos-v5
mkdir -p "$HERE/out"
cp .pio/build/kinetos-v5/firmware.bin "$HERE/out/kinetos-openevse-$VERSION.bin"
cp .pio/build/kinetos-v5/firmware.elf "$HERE/out/kinetos-openevse-$VERSION.elf"
sha256sum "$HERE/out/kinetos-openevse-$VERSION.bin"
