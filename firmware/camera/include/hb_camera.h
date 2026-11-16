// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "hb_frame_pool.h"
#include "hb_light_meter.h"
#include <stdbool.h>
#include <stdint.h>
// Read-only identification on the existing board I2C bus. No reset/power GPIO writes.
esp_err_t hb_camera_probe(uint16_t *sensor_id);
typedef struct
{
    // ISP RGB888 native packed B,G,R; immutable while leased.
    const uint8_t *pixels;
    unsigned width, height;
    unsigned rotation_ccw; // 90 degrees: canonical upright dimensions are height x width.
    uint32_t sequence;
    uint64_t timestamp_ms;
    bool settled;
    hb_frame_lease lease;
} hb_camera_frame;
typedef enum {
    HB_CAM_IDLE=0, HB_CAM_PROBE, HB_CAM_SENSOR_CONFIG, HB_CAM_CSI_CREATE,
    HB_CAM_PSRAM, HB_CAM_CSI_ENABLE, HB_CAM_ISP_CREATE, HB_CAM_ISP_CONFIG,
    HB_CAM_CSI_START, HB_CAM_SENSOR_STREAM, HB_CAM_WAIT_FRAMES,
    HB_CAM_STREAMING, HB_CAM_RETRY, HB_CAM_FAILED
} hb_camera_stage;
typedef struct
{
    uint16_t sensor_id;
    hb_camera_stage stage;
    uint64_t stage_since_ms;
    uint32_t failed_attempts;
    unsigned width, height;
    uint32_t frames, dropped, errors, restarts, retry_seconds;
    float fps, exposure_us, analogue_gain, digital_gain;
    uint64_t light_ms;
    hb_light_reading light;
    esp_err_t error;
    bool running;
    bool test_pattern;
    bool quarantined;
} hb_camera_status;
esp_err_t hb_camera_start(void);
// Renew local on-demand capture for 3.5 s. No camera work when no consumer.
void hb_camera_request(void);
void hb_camera_get_status(hb_camera_status *status);
bool hb_camera_acquire(hb_camera_frame *frame);
void hb_camera_release(hb_camera_frame *frame);
void hb_camera_set_sample_seconds(uint32_t seconds);
void hb_camera_test_pattern(bool enabled);

// Upright native face rectangle; expires automatically, never holds a camera lease.
void hb_camera_face_hint(unsigned x, unsigned y, unsigned w, unsigned h, uint64_t frame_ms);
