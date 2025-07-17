#include <SPI.h>
#include <TFT_eSPI.h>
#include "esp_camera.h"

#define TFT_GREY 0x5AEB
// ===================
// Select camera model
// ===================
//define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
// ** Espressif Internal Boards **
//define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
#include "camera_pins.h"

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");

  // Camera config (RGB565 mode)
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.pixel_format = PIXFORMAT_RGB565;  // <<-- RGB565 for BMP
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 1;
  if(psramFound()){
    config.jpeg_quality = 15;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Limit the frame size when PSRAM is not available
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  // Adjust sensor settings
  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_brightness(s, 1);
  s->set_saturation(s, 0);

  // Initialize TFT
  Serial.println("Initializing TFT");
  tft.begin();
  tft.setRotation(1);
  Serial.println("FillingScreen");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.println("Camera Ready (BMP Mode)");
  delay(1000);
}

void loop() {
  // Capture frame (RGB565)
  Serial.println("Grabbing");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    return;
  }

  // Display RGB565 directly on TFT
  Serial.println("Pushing");
  tft.pushImage(0, 0, 320, 240, (uint16_t *)fb->buf);  // Assumes QVGA (320x240)

  // Release frame buffer
  esp_camera_fb_return(fb);

  // Small delay (~10FPS)
  delay(50);
}