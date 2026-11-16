#!/usr/bin/env python3
"""Project-local ESP-IDF 6.1 CSI override, never modify the installed SDK.
Adapt two audited Home Buddy DMA fixes to native IDF6.1. Preserve logical
frame length while allowing cache maintenance of the padded DMA allocation.
"""
import os, shutil, sys
from pathlib import Path
up=Path(sys.argv[1]).resolve()
native=Path(os.environ["IDF_PATH"])/"components/esp_driver_cam"
dst=up/"components/esp_driver_cam"
p=native/"csi/src/esp_cam_ctlr_csi.c"
data=p.read_text()
def patch(old,new):
    global data
    if data.count(old)!=1:
        raise RuntimeError("Unexpected IDF 6.1 CSI source: "+old)
    data=data.replace(old,new,1)
patch("ctlr->h_res * ctlr->v_res * ctlr->in_bpp / 64",
      "ctlr->h_res * ctlr->v_res * ctlr->out_bpp / 64")
# IDF 6.1 disallows UNALIGNED for DIR_M2C (esp_cache_msync.c).
# The RGB888 DMA frame has an incomplete final cache line. All buffers
# carry their own 256-byte padding. Invalidate rounded lengths instead.
patch('#include "esp_cache.h"', '''#include "esp_cache.h"
#define ESPEN_CSI_CACHE_ALIGN 256U
#define ESPEN_CSI_CACHE_BYTES(n) (((n) + ESPEN_CSI_CACHE_ALIGN - 1) & ~(ESPEN_CSI_CACHE_ALIGN - 1))''')
patch("ctlr->trans.buffer), ctlr->trans.received_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "ctlr->trans.buffer), ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes), ESP_CACHE_MSYNC_FLAG_DIR_M2C")
# The CSI fallback buffer must carry the same 256-byte padding
# as the three application buffers (FRAME_ALLOC_BYTES).
patch("heap_caps_aligned_alloc(alignment, ctlr->fb_size_in_bytes, MALLOC_CAP_SPIRAM)",
      "heap_caps_aligned_alloc(MAX(alignment, ESPEN_CSI_CACHE_ALIGN), ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes), MALLOC_CAP_SPIRAM)")
patch("ctlr->backup_buffer), ctlr->fb_size_in_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M",
      "ctlr->backup_buffer), ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes), ESP_CACHE_MSYNC_FLAG_DIR_C2M")
patch("new_trans.buffer), new_trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "new_trans.buffer), ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes), ESP_CACHE_MSYNC_FLAG_DIR_M2C")
patch("trans.buffer), trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "trans.buffer), ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes), ESP_CACHE_MSYNC_FLAG_DIR_M2C")
# P4 rev1 cannot use the new bridge color converter. The CSI input is
# RAW10; the separate hardware ISP produces RGB888 downstream. IDF6.1
# otherwise incorrectly rejects this pair as a bridge conversion request.
patch("if (ctlr->custom_data_depth || config->src_format == config->dst_format) {",
      "if (ctlr->custom_data_depth || config->src_format == config->dst_format || "
      "(config->src_format == CAM_CTLR_COLOR_RAW10 && config->dst_format == CAM_CTLR_COLOR_RGB888)) {")
# Keep logical format metadata consistent even when the separate ISP owns conversion.
patch("mipi_csi_brg_ll_set_color_mode_bypass(ctlr->hal.bridge_dev, true);\n        return ESP_OK;",
      "mipi_csi_brg_ll_set_color_mode_bypass(ctlr->hal.bridge_dev, true);\n"
      "        ctlr->in_color_format = config->src_format;\n"
      "        ctlr->out_color_format = config->dst_format;\n        return ESP_OK;")
if dst.exists():shutil.rmtree(dst)
shutil.copytree(native,dst)
(dst/"csi/src/esp_cam_ctlr_csi.c").write_text(data)
# Contract: all four cache calls (3 x M2C + 1 x C2M) use the
# allocated rounded length without unsupported UNALIGNED flags.
assert data.count("ESPEN_CSI_CACHE_BYTES(ctlr->fb_size_in_bytes)") == 5
assert "ESP_CACHE_MSYNC_FLAG_UNALIGNED" not in data
assert data.count("ESP_CACHE_MSYNC_FLAG_DIR_M2C") == 3
print("Espen: local IDF6.1 CSI aligned M2C DMA adapter generated")
