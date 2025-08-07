#include <SPI.h>
#include <TFT_eSPI.h>
#include "esp_camera.h"

// Shared volatile flag for interrupt communication
volatile bool captureRequested = false;
volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200; // 200ms debounce

#define TFT_GREY 0x5AEB
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

TFT_eSPI tft = TFT_eSPI();

// Use a different GPIO pin (GPIO4 is safer than GPIO1)
#define TRIGGER_PIN 1  // Changed from GPIO1 to avoid UART conflict
int val = 0; 

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");
  pinMode(2, INPUT);
  
  

  // Camera config
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
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_RGB565;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 1;
  
  if(psramFound()){
    Serial.println("Found");
    config.jpeg_quality = 15;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("NotFound");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    //config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while(1); // Halt if camera fails
  }

  // Sensor settings
  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_brightness(s, -1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 2);
  s->set_special_effect(s, 0);
  s->set_aec2(s, 1);

  // Initialize TFT
  tft.begin();
  if (!tft.width() || !tft.height()) {
    Serial.println("TFT initialization failed");
    while(1); // Halt if TFT fails
  }
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.println("Camera Ready");

  // Setup interrupt
  pinMode(TRIGGER_PIN, INPUT);
  attachInterrupt(TRIGGER_PIN, GrabOneImg, FALLING);//digitalPinToInterrupt(TRIGGER_PIN), GrabOneImg, FALLING);

  Serial.println("System Ready");
}

void GrabOneImg() {
  unsigned long currentTime = millis();
  //Serial.println(captureRequested);
  if (currentTime - lastInterruptTime > debounceDelay) {
    captureRequested = true;
    lastInterruptTime = currentTime;
    
  }
}

void loop() {
  if (captureRequested) {
    captureRequested = false;
    Serial.println("Capturing image...");

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Capture failed");
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0);
      tft.println("Capture Failed");
      return;
    }

    tft.pushImage(0, 0, 320, 240, (uint16_t *)fb->buf);
    esp_camera_fb_return(fb);
    
    Serial.println("Image displayed");
  }
  val = analogRead(2);
  Serial.println(val);
  delay(1000); // Small delay to prevent watchdog issues
}