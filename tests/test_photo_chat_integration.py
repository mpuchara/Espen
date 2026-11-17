#!/usr/bin/env python3
"""Static integration guard; physical P4 camera/LVGL runtime still needs QA."""
from pathlib import Path

camera = Path("firmware/espen_camera.cc").read_text(encoding="utf-8")
api = Path("firmware/espen_camera.h").read_text(encoding="utf-8")
display = Path("firmware/espen_board.cc").read_text(encoding="utf-8")
gate = Path("firmware/vision_gate.h").read_text(encoding="utf-8")

checks = [
    ("Cloud vision defaults ON but can be overridden from NVS",
     'GetBool("cloud_enabled",true)' in camera),
    ("Camera takes explicit per-turn permission",
     "espen::VisionQuestion(text)" in camera and "grant_.exchange(0" in camera),
    ("The same settled real sensor frame feeds cloud JPEG and chat",
     "hb_rgb888_thumbnail_clockwise(f.frame.pixels" in camera
     and "hb_rgb888_preview_clockwise(" in camera
     and "f.frame.pixels,f.frame.width,f.frame.height," in camera
     and "status.test_pattern" in camera),
    ("Photo transferred from CSI to UI under independent mutex",
     "chat_photo_mutex_" in api and "TakeChatPhoto()" in camera),
    ("Photo notification sent only after JPEG success",
     camera.index("if(chat_rgb&&CloudEnabled())") > camera.index("jpeg_at_=esp_timer_get_time();")),
    ("LVGL renders image using real RGB565 snapshot",
     "lv_image_set_src(photo,&data->image)" in display
     and "LV_COLOR_FORMAT_RGB565" in display
     and "TakeChatPhoto()" in display),
    ("UI owns and deletes volatile PSRAM chat image",
     "LV_EVENT_DELETE,data" in display
     and "heap_caps_free(attachment->pixels)" in display),
    ("Chat photo bounds enforced",
     "kPhotoHistoryLimit = 6" in display),
    ("UI does not block on camera HTTP upload",
     "std::mutex chat_photo_mutex_" in api),
    ("Local photo no auto-send without recognized request",
     "Co widzisz?" not in gate and '" co widzisz "' in gate
     and '" zrob zdjecie "' in gate),
]
failed = [description for description, passed in checks if not passed]
if failed:
    raise SystemExit("Camera/chat integration regression: " + ", ".join(failed))
print("Camera/chat source integration checks passed")
