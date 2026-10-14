#pragma once

/* Network identity reserved for the future vision edge node. */
#define CAMERA_DEVICE_ID                "cam01"

/* Hardware identity. This project targets the original ESP32, not ESP32-S3. */
#define CAMERA_BOARD_MODEL              "AI Thinker style ESP32-CAM"
#define CAMERA_SENSOR_MODEL             "OV2640"

/* High-resolution still photography; the backend requests a fresh frame. */
#define CAMERA_FRAME_SIZE               FRAMESIZE_UXGA
#define CAMERA_JPEG_QUALITY             12
#define CAMERA_FRAME_BUFFER_COUNT       1

/* Network behavior. Image bytes use HTTP; MQTT only reports status and URLs. */
#define CAMERA_STATUS_PERIOD_MS         3000
