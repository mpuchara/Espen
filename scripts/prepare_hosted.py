#!/usr/bin/env python3
"""ESP32-P4 rev1 / C6 SDIO reliability overlay for ESP-Hosted 2.13.1.

Keep large queues in PSRAM but never DMA directly into PSRAM or an unaligned
register buffer. The observed 0xCCCCC RX length indicates corrupt register
data, not an SSID/password failure. Preserve upstream SDIO streaming protocol.
"""
import sys
from pathlib import Path

up = Path(sys.argv[1]).resolve()
hosted = up / "local_components/esp-hosted"
def edit(path, old, new, count=1):
    p = Path(path)
    s = p.read_text()
    if s.count(old) != count:
        raise RuntimeError(f"Unexpected source {p}: {old[:100]}")
    p.write_text(s.replace(old, new))

edit(up / "main/idf_component.yml",
     '  espressif/esp_hosted:\n    version: ^2.12.11',
     '  espressif/esp_hosted:\n    version: "==2.13.1"\n    override_path: ../local_components/esp-hosted')

# The 2.13.1 aligned allocator prefers PSRAM for ALL aligned allocations,
# including small SDIO register buffers. Use internal DMA staging at the
# SDMMC boundary, keeping the large upper-level pools in PSRAM.
port = hosted / "host/port/esp/freertos/src/port_esp_hosted_host_sdio.c"
edit(port, '#include "esp_log.h"', '#include "esp_log.h"\n#include "esp_attr.h"\n#include <string.h>')
edit(port, 'static void * sdio_bus_lock;',
     '''static void * sdio_bus_lock;

// Each direction has its own staging buffer; upstream SDIO driver serializes
// calls with its bus lock (or the HAL lock when lock_required is true).
// Internal DRAM, cache-line aligned for ESP32-P4 SDMMC DMA.
#define ESPEN_SDIO_STAGE_SIZE 2048
static DMA_ATTR uint8_t espen_rx_dma[ESPEN_SDIO_STAGE_SIZE] __attribute__((aligned(128)));
static DMA_ATTR uint8_t espen_tx_dma[ESPEN_SDIO_STAGE_SIZE] __attribute__((aligned(128)));''')
edit(port,
     'blocks = H_SDIO_RX_BLOCKS_TO_TRANSFER(remainder);\n\t\tsize = blocks * ESP_BLOCK_SIZE;\n\t\tres = sdmmc_io_read_blocks(card, function, addr, ptr, size);\n\t\tif (res)\n\t\t\treturn res;',
     '''blocks = H_SDIO_RX_BLOCKS_TO_TRANSFER(remainder);
		if (blocks > ESPEN_SDIO_STAGE_SIZE / ESP_BLOCK_SIZE)
			blocks = ESPEN_SDIO_STAGE_SIZE / ESP_BLOCK_SIZE;
		size = blocks * ESP_BLOCK_SIZE;
		res = sdmmc_io_read_blocks(card, function, addr, espen_rx_dma, size);
		if (res)
			return res;
		memcpy(ptr, espen_rx_dma, size);''')
edit(port,
     'res = sdmmc_io_read_bytes(card, function, addr, ptr, size);\n\t\tif (res)\n\t\t\treturn res;',
     '''res = sdmmc_io_read_bytes(card, function, addr, espen_rx_dma, size);
		if (res)
			return res;
		memcpy(ptr, espen_rx_dma, size);''')
edit(port,
     'blocks = H_SDIO_TX_BLOCKS_TO_TRANSFER(remainder);\n\t\tsize = blocks * ESP_BLOCK_SIZE;\n\t\tres = sdmmc_io_write_blocks(card, function, addr, ptr, size);',
     '''blocks = H_SDIO_TX_BLOCKS_TO_TRANSFER(remainder);
		if (blocks > ESPEN_SDIO_STAGE_SIZE / ESP_BLOCK_SIZE)
			blocks = ESPEN_SDIO_STAGE_SIZE / ESP_BLOCK_SIZE;
		size = blocks * ESP_BLOCK_SIZE;
		memcpy(espen_tx_dma, ptr, size);
		res = sdmmc_io_write_blocks(card, function, addr, espen_tx_dma, size);''')
edit(port,
     'res = sdmmc_io_write_bytes(card, function, addr, ptr, size);',
     'memcpy(espen_tx_dma, ptr, size);\n\t\tres = sdmmc_io_write_bytes(card, function, addr, espen_tx_dma, size);')
edit(port,
     'res = sdmmc_io_read_bytes(card, SDIO_FUNC_1, reg, data, size);',
     '''if (size > ESPEN_SDIO_STAGE_SIZE) {
			res = ESP_ERR_INVALID_SIZE;
		} else {
			res = sdmmc_io_read_bytes(card, SDIO_FUNC_1, reg, espen_rx_dma, size);
			if (res == ESP_OK) memcpy(data, espen_rx_dma, size);
		}''')
edit(port,
     'res = sdmmc_io_write_bytes(card, SDIO_FUNC_1, reg, data, size);',
     '''if (size > ESPEN_SDIO_STAGE_SIZE) {
			res = ESP_ERR_INVALID_SIZE;
		} else {
			memcpy(espen_tx_dma, data, size);
			res = sdmmc_io_write_bytes(card, SDIO_FUNC_1, reg, espen_tx_dma, size);
		}''')
edit(port, 'config.max_freq_khz = sdio_config->clock_freq_khz;',
     'config.max_freq_khz = sdio_config->clock_freq_khz;\n\tESP_LOGI(TAG, "Espen SDIO: internal DMA staging enabled (RX/TX 2048 bytes)");')

drv = hosted / "host/drivers/transport/sdio/sdio_drv.c"
# Make the reset pulse robust across host-only resets, without changing the
# verified GPIO or upstream polarity/sequence.
edit(drv,
     'g_h.funcs->_h_write_gpio(reset_pin.port, reset_pin.pin, H_RESET_VAL_INACTIVE);\n\tg_h.funcs->_h_msleep(10);',
     'g_h.funcs->_h_write_gpio(reset_pin.port, reset_pin.pin, H_RESET_VAL_INACTIVE);\n\tg_h.funcs->_h_msleep(100);')
# The register 0xCCCCCCCC observed as 838860 (0xCCCCC after masking) is
# implausible for a transfer passed into the uint16_t HAL. Guard both the
# poison pattern and length before allocation or address arithmetic.
edit(drv, 'if (reg_val != UINT32_MAX)\n\t\treturn false;',
     'if (reg_val != UINT32_MAX && reg_val != 0xCCCCCCCCu)\n\t\treturn false;')
edit(drv, '(all 32 bits set): SDIO bus fault', '(invalid register pattern): SDIO bus fault')
edit(drv,
     '\t\tif (ret || !len_from_slave) {',
     '''		if (len_from_slave > 65024u) {
			ESP_LOGE(TAG, "Espen SDIO: invalid RX length %lu, discarding corrupt register",
			         (unsigned long)len_from_slave);
			SDIO_DRV_UNLOCK();
			g_h.funcs->_h_event_post(ESP_HOSTED_EVENT, ESP_HOSTED_EVENT_TRANSPORT_FAILURE,
			                        NULL, 0, HOSTED_BLOCK_MAX);
			continue;
		}
		if (ret || !len_from_slave) {''')

# Do not reset an already running SDIO RX task underneath an in-flight DMA
# operation. A failed handshake returns a real error instead of restarting
# the whole P4. A physical restart or user restart can try again safely.
transport = hosted / "host/drivers/transport/transport_drv.c"
edit(transport,
     '''				if (retry_slave_connection%50==0) {
					ESP_LOGI(TAG, "Not able to connect with ESP-Hosted slave device");
					if (ESP_OK != ensure_slave_bus_ready(bus_handle)) {
						ESP_LOGE(TAG, "ensure_slave_bus_ready failed");
						return ESP_FAIL;
					}
				}''',
     '''				if (retry_slave_connection >= 50) {
					ESP_LOGE(TAG, "Espen SDIO: handshake timed out; no automatic host reset");
					retry_slave_connection = 0;
					return ESP_ERR_TIMEOUT;
				}''')
api = hosted / "host/api/src/esp_hosted_api.c"
edit(api, 'ESP_ERROR_CHECK_WITHOUT_ABORT(transport_drv_reconfigure());\n\treturn ESP_OK;',
     'return transport_drv_reconfigure();')
edit(api,
     'ESP_ERROR_CHECK_WITHOUT_ABORT(esp_hosted_reconfigure());\n\tcheck_transport_up();\n\treturn rpc_wifi_init(arg);',
     'esp_err_t ret = esp_hosted_reconfigure();\n\tif (ret != ESP_OK) return ret;\n\tcheck_transport_up();\n\treturn rpc_wifi_init(arg);')

# If the C6 handshake still fails, leave the UI running instead of proceeding
# into invalid station/AP state. Saved credentials and pairing remain intact.
board = up / "main/boards/common/wifi_board.cc"
edit(board, '    wifi_manager.Initialize(config);',
     '''    if (!wifi_manager.Initialize(config)) {
        ESP_LOGE(TAG, "Espen: C6 transport initialization failed; UI remains available");
        GetDisplay()->SetStatus("Wi-Fi: błąd komunikacji C6");
        extern void EspenCloudInfo(const char*, const char*);
        EspenCloudInfo("Błąd komunikacji P4-C6 (SDIO). Zrestartuj urządzenie.", "");
        return;
    }''')
# P4 does not have a local Wi-Fi MAC; use its eFuse base only for DHCP hostname
# (NOT the actual C6 MAC used by Xiaozhi activation).
edit(board, 'if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {',
     'if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {')
