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
patch("ctlr->trans.buffer), ctlr->trans.received_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "ctlr->trans.buffer), ctlr->fb_size_in_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED")
# The 1288x728 RGB888 logical length is not divisible by a 128-byte
# cache line. Allocate padding so unaligned cache maintenance cannot
# touch another object's bytes. The DMA still transfers the logical size.
patch("heap_caps_aligned_alloc(alignment, ctlr->fb_size_in_bytes, MALLOC_CAP_SPIRAM)",
      "heap_caps_aligned_alloc(alignment, ((ctlr->fb_size_in_bytes + alignment - 1) / alignment) * alignment, MALLOC_CAP_SPIRAM)")
patch("ctlr->backup_buffer), ctlr->fb_size_in_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M",
      "ctlr->backup_buffer), ctlr->fb_size_in_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED")
patch("new_trans.buffer), new_trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "new_trans.buffer), new_trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED")
patch("trans.buffer), trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C",
      "trans.buffer), trans.buflen, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED")
if dst.exists():shutil.rmtree(dst)
shutil.copytree(native,dst)
(dst/"csi/src/esp_cam_ctlr_csi.c").write_text(data)
print("Espen: local IDF6.1 CSI DMA/output-cache adapter generated")
