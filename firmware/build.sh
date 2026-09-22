#!/usr/bin/env bash
# Build OpenEVSE v5.1.5 + Kinetos board support (MID meter, smart1, pins) for the WT32-ETH01.
# Output: out/openevse-<tag>-kinetos-mid.bin (flash via the wallbox's /update page)
set -euo pipefail
TAG="${OPENEVSE_TAG:-v5.1.5}"
KINETOS_REV="${KINETOS_REV:-1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$HERE/.work}"
if [ ! -d "$WORK/.git" ]; then
  git clone -q https://github.com/OpenEVSE/ESP32_WiFi_V4.x.git "$WORK"
fi
cd "$WORK"
git fetch -q --tags
git checkout -q -f --detach "$TAG"
git clean -qfd src
git submodule update -q --init --recursive
# GUI: Kinetos page, TeslaMate, no OhmConnect/Tesla owner API, English only
git -C gui-v2 checkout -q -- . && git -C gui-v2 clean -qfd src
python3 "$HERE/gui/apply.py" gui-v2
(cd gui-v2 && npm ci --no-audit --no-fund --silent && npm run build --silent)
git -C gui-v2 add -A src && git -C gui-v2 -c user.name=build -c user.email=build@localhost commit -qm "kinetos gui" --allow-empty
git add gui-v2
git apply "$HERE"/patches/*.patch
cp "$HERE"/kinetos/*.h "$HERE"/kinetos/*.cpp src/
# Clean tree + tag name so the firmware reports e.g. "v5.1.5-kinetos.1" (evcc parses x.y.z)
git -c user.name=build -c user.email=build@localhost commit -qam "kinetos build" --allow-empty
git add -A src && git -c user.name=build -c user.email=build@localhost commit -qm "kinetos sources" --allow-empty
export GITHUB_REF_NAME="$TAG-kinetos.$KINETOS_REV"
if command -v pio >/dev/null; then PIO=(pio); else PIO=(uv tool run --from platformio pio); fi
# First pass generates the GUI/LCD headers into src/, commit them so the version is not "_modified"
"${PIO[@]}" run -e wt32-eth01-kinetos-mid
git add -A src && git -c user.name=build -c user.email=build@localhost commit -qm "generated headers" --allow-empty
"${PIO[@]}" run -e wt32-eth01-kinetos-mid
mkdir -p "$HERE/out"
cp .pio/build/wt32-eth01-kinetos-mid/firmware.bin "$HERE/out/openevse-$TAG-kinetos-mid.bin"
cp .pio/build/wt32-eth01-kinetos-mid/firmware.elf "$HERE/out/openevse-$TAG-kinetos-mid.elf"
sha256sum "$HERE/out/openevse-$TAG-kinetos-mid.bin"
