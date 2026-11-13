#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
ref=49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
if [ ! -d upstream/.git ]; then
  git clone --depth 1 --branch v2.5.2 https://github.com/78/xiaozhi-esp32.git upstream
fi
git -C upstream fetch --depth 1 origin "$ref"
git -C upstream reset --hard "$ref"
python3 scripts/prepare.py upstream
cd upstream
python3 scripts/build.py --list-boards --json > /tmp/espen-board-list.json
python3 scripts/build.py guition/espen-guition-jc4880p443 --name espen-guition-jc4880p443 --language pl-PL --wake-word disabled
# Verify the actual generated configuration, not only the requested defaults.
for setting in \
  CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y \
  CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE=10 \
  CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE=10; do
  grep -Fx "$setting" sdkconfig || { echo "Missing required setting: $setting"; exit 1; }
done
grep -Fx '# CONFIG_ESP_HOSTED_SDIO_RX_DOUBLE_BUFFERING is not set' sdkconfig
test -s build/merged-binary.bin
# Full image is for P4 rev1 and must fit its 16 MiB flash.
test "$(stat -c %s build/merged-binary.bin)" -le 16777216
cd "$root"
mkdir -p dist
cp -r web/. dist/
cp upstream/build/merged-binary.bin dist/espen.bin
cp upstream/LICENSE dist/xiaozhi-LICENSE.txt
cp firmware/vendor-LICENSE dist/panel-LICENSE.txt
(cd dist && sha256sum espen.bin > espen.bin.sha256)
