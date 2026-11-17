#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
# Verify all firmware NVS keys obey the 15-byte ESP-IDF limit.
python3 tests/test_nvs_key_lengths.py
python3 - <<'PYVERSION'
import json
from pathlib import Path
for file in ("web/manifest.json", "web/manifest-full.json"):
    assert json.loads(Path(file).read_text())["version"] == "0.2.11", file
assert "0.2.11 EXPERIMENTAL" in Path("web/index.html").read_text()
assert "Espen 0.2.11 boot:" in Path("firmware/espen_board.cc").read_text()
PYVERSION
# Source-level integration guards for the LVGL/MCP bridge (plus host vision gate tests).
python3 tests/test_photo_chat_integration.py
# Host-side regression tests for the local wake feature extractor and DTW.
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_wake_features.cc -o /tmp/espen-wake-test
/tmp/espen-wake-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Itests/stubs tests/test_wake_engine.cc -o /tmp/espen-wake-engine-test
/tmp/espen-wake-engine-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_face_engine.cc -o /tmp/espen-face-test
/tmp/espen-face-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_speech_onset.cc -o /tmp/espen-speech-test
/tmp/espen-speech-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_social_reactions.cc -o /tmp/espen-social-test
/tmp/espen-social-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_speech_quality.cc -o /tmp/espen-quality-test
/tmp/espen-quality-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_visual_world.cc -o /tmp/espen-visual-test
/tmp/espen-visual-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_speaker_audio.cc -o /tmp/espen-speaker-test
/tmp/espen-speaker-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_emotional_audio.cc -o /tmp/espen-emotional-audio-test
/tmp/espen-emotional-audio-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_vision_gate.cc -o /tmp/espen-vision-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_vision_endpoint.cc -o /tmp/espen-vision-endpoint-test
/tmp/espen-vision-endpoint-test
/tmp/espen-vision-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_voice_commands.cc -o /tmp/espen-voice-command-test
/tmp/espen-voice-command-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_barge_in.cc -o /tmp/espen-barge-test
/tmp/espen-barge-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_standby_policy.cc -o /tmp/espen-standby-test
/tmp/espen-standby-test
cc -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/camera/include tests/test_camera_rotation.c firmware/camera/raw_frame.c firmware/camera/light_meter.c -lm -o /tmp/espen-camera-rotation-test
/tmp/espen-camera-rotation-test
ref=49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
if [ ! -d upstream/.git ]; then
  git clone --depth 1 --branch v2.5.2 https://github.com/78/xiaozhi-esp32.git upstream
fi
git -C upstream fetch --depth 1 origin "$ref"
git -C upstream reset --hard "$ref"
# Pin the Wi-Fi library to the version verified in v0.1.4, and use a local
# component override so our changes are never overwritten by the IDF resolver.
wifi_ref=77095063d77e7705f68e95005c43ed73582233ce
mkdir -p upstream/local_components
if [ ! -d upstream/local_components/esp-wifi-connect/.git ]; then
  git clone --depth 1 --branch v3.3.3 https://github.com/78/esp-wifi-connect.git upstream/local_components/esp-wifi-connect
fi
git -C upstream/local_components/esp-wifi-connect fetch --depth 1 origin "$wifi_ref"
git -C upstream/local_components/esp-wifi-connect reset --hard "$wifi_ref"
hosted_ref=0e8d0e3f0c2723deaf15441200cb4d8c53fea230
if [ ! -d upstream/local_components/espressif__esp_hosted/.git ]; then
  git clone --depth 1 --branch v2.13.1 https://github.com/espressif/esp-hosted-mcu.git upstream/local_components/espressif__esp_hosted
fi
git -C upstream/local_components/espressif__esp_hosted fetch --depth 1 origin "$hosted_ref"
git -C upstream/local_components/espressif__esp_hosted reset --hard "$hosted_ref"
# A registry package includes protobuf-c, but a git checkout does not.
# Initialize the exact submodule recorded by the pinned upstream commit.
git -C upstream/local_components/espressif__esp_hosted submodule update --init --recursive common/protobuf-c
test "$(git -C upstream/local_components/espressif__esp_hosted/common/protobuf-c rev-parse HEAD)" = abc67a11c6db271bedbb9f58be85d6f4e2ea8389
test -s upstream/local_components/espressif__esp_hosted/common/protobuf-c/protobuf-c/protobuf-c.c
test -s upstream/local_components/espressif__esp_hosted/common/protobuf-c/protobuf-c/protobuf-c.h

python3 scripts/prepare.py upstream
# Verify both hardware-unmute and 75% defaults in the generated source.
grep -Fq 'int output_volume_ = 75;' upstream/main/audio/audio_codec.h
grep -Fq 'Espen speaker: PA GPIO%d level=%d DAC volume=%d%% output=%d gpio_err=%s' upstream/main/audio/codecs/es8311_audio_codec.cc
grep -Fq 'Espen speaker PCM sent:' upstream/main/audio/codecs/es8311_audio_codec.cc
# Crash regression guard: no NSNet2 FFT path may be enabled by persisted NVS.
grep -Fq 'afe_config->ns_init = false;' upstream/main/audio/engines/afe_audio_engine.cc
grep -Fq 'Espen AFE safe mode: NSNet2 disabled' upstream/main/audio/engines/afe_audio_engine.cc
python3 scripts/prepare_camera_idf.py upstream
python3 scripts/prepare_wifi.py upstream
python3 scripts/prepare_wifi_reliability.py upstream
python3 scripts/prepare_hosted.py upstream
cd upstream
python3 scripts/build.py --list-boards --json > /tmp/espen-board-list.json
python3 scripts/build.py guition/espen-guition-jc4880p443 --name espen-guition-jc4880p443 --language pl-PL
# Verify the actual generated configuration, not only the requested defaults.
for setting in \
  CONFIG_WAKE_WORD_DISABLED=y \
  CONFIG_USE_AUDIO_PROCESSOR=y \
  CONFIG_CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE=y \
  CONFIG_SR_NSN_NSNET2=y \
  CONFIG_SR_VADN_VADNET1_MEDIUM=y \
  CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y \
  CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000 \
  CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS=2500 \
  CONFIG_ESP_HOSTED_PKT_STATS=y \
  CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE=10 \
  CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE=10; do
  grep -Fx "$setting" sdkconfig || { echo "Missing required setting: $setting"; exit 1; }
done
grep -Fx '# CONFIG_ESP_HOSTED_SDIO_RX_DOUBLE_BUFFERING is not set' sdkconfig
grep -Fx '# CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE is not set' sdkconfig
test -s build/merged-binary.bin
# Full image is for P4 rev1 and must fit its 16 MiB flash.
test "$(stat -c %s build/merged-binary.bin)" -le 16777216
cd "$root"
mkdir -p dist
cp -r web/. dist/
# Guard the non-destructive update layout against future upstream changes.
python3 - <<'PYLAYOUT'
import json
from pathlib import Path
build = Path("upstream/build")
flash = json.loads((build / "flasher_args.json").read_text())["flash_files"]
offsets = {Path(name).name: int(address, 0) for address, name in flash.items()}
assert offsets.get("xiaozhi.bin") == 0x20000, offsets
assert offsets.get("generated_assets.bin") == 0x800000, offsets
assert (build / "xiaozhi.bin").stat().st_size <= 0x3f0000
assert (build / "generated_assets.bin").stat().st_size <= 0x800000
manifest = json.loads(Path("web/manifest.json").read_text())
assert manifest["builds"][0]["parts"] == [
    {"path": "espen-app.bin", "offset": 0x20000},
    {"path": "espen-assets.bin", "offset": 0x800000},
]
PYLAYOUT
cp upstream/build/merged-binary.bin dist/espen.bin
cp upstream/build/xiaozhi.bin dist/espen-app.bin
cp upstream/build/generated_assets.bin dist/espen-assets.bin
cp upstream/LICENSE dist/xiaozhi-LICENSE.txt
cp firmware/vendor-LICENSE dist/panel-LICENSE.txt
cp firmware/NOTO-OFL.txt dist/noto-LICENSE.txt
cp firmware/camera/vendor/LICENSE dist/camera-LICENSE.txt
cp firmware/camera/vendor/SOURCE.md dist/camera-SOURCE.md
(cd dist && sha256sum espen.bin > espen.bin.sha256 && sha256sum espen-app.bin espen-assets.bin > espen-update.sha256)
