# Camera owner and OV02C10 register sequence ported from Home Buddy.
idf_component_register(SRCS "camera_probe.c" "camera_capture.c" "camera_health.c" "frame_pool.c" "light_meter.c" "raw_frame.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2c esp_driver_cam esp_driver_isp esp_timer esp_psram esp_mm
    LDFRAGMENTS "dw_gdma_cache.lf")
target_compile_definitions(${COMPONENT_LIB} PRIVATE HB_CAMERA_DMA_OUTPUT_BITS=1)
