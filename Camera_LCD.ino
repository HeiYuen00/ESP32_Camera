#include <SPI.h>
#include <TFT_eSPI.h>
#include "esp_camera.h"
#include "sd_read_write.h"

// Shared volatile flag for interrupt communication
volatile bool captureRequested = false;
volatile unsigned long lastInterruptTime = 0;
volatile unsigned long lastUpdateExposureTime = 0;
const unsigned long debounceDelay = 200; // 200ms debounce
const unsigned long UpdateExposureDelay = 5000; // 1000ms debounce

#define TFT_GREY 0x5AEB
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"


TFT_eSPI tft = TFT_eSPI();

#define TRIGGER_PIN 1
#define Value_PIN  2
int val = 0;
int mappedAEC = 300;
int lastAEC = 300;
int tint[3] = {255, 255, 255};
int photo_index = 0;


void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");
  pinMode(2, INPUT);
  
  //SD Card Config
  sdmmcInit();
  createDir(SD_MMC, "/camera");
  listDir(SD_MMC, "/camera", 0);
  
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
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while(1); // Halt if camera fails
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_brightness(s, -1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 2);
  s->set_special_effect(s, 0);
  
  // 关键曝光控制设置
  s->set_exposure_ctrl(s, 0);   // 启用手动曝光
  s->set_aec2(s, 1);            // 禁用AEC2
  s->set_gain_ctrl(s, 0);       // 禁用自动增益控制
  s->set_agc_gain(s, 0);        // 固定增益值
  //s->set_aec_value(s, mappedAEC);

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
  attachInterrupt(TRIGGER_PIN, GrabOneImg, FALLING);

  Serial.println("System Ready");
}

void GrabOneImg() {
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime > debounceDelay) {
    captureRequested = true;
    lastInterruptTime = currentTime;
  }
}

void applyRGBtint(uint16_t* imageBuffer, int width, int height, const int rgbTint[3]) {
    // Extract tint components (0-255)
    int rTint = rgbTint[0];
    int gTint = rgbTint[1];
    int bTint = rgbTint[2];

    // Normalize tint factors (0.0-1.0)
    float rFactor = rTint / 255.0f;
    float gFactor = gTint / 255.0f;
    float bFactor = bTint / 255.0f;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Get current pixel index
            int index = y * width + x;
            
            // Extract RGB565 components
            uint16_t pixel = imageBuffer[index];
            uint8_t r = (pixel >> 11) & 0x1F;  // 5 bits
            uint8_t g = (pixel >> 5)  & 0x3F;   // 6 bits
            uint8_t b = pixel & 0x1F;           // 5 bits

            // Convert to 8-bit (for better tint calculation)
            r = (r << 3) | (r >> 2);  // 5->8 bits
            g = (g << 2) | (g >> 4);  // 6->8 bits
            b = (b << 3) | (b >> 2);  // 5->8 bits

            // Apply tint (multiplicative blending)
            r = static_cast<uint8_t>(r * rFactor);
            g = static_cast<uint8_t>(g * gFactor);
            b = static_cast<uint8_t>(b * bFactor);

            // Convert back to RGB565
            r = r >> 3;  // 8->5 bits
            g = g >> 2;  // 8->6 bits
            b = b >> 3;  // 8->5 bits

            // Recombine into 16-bit pixel
            imageBuffer[index] = (r << 11) | (g << 5) | b;
        }
    }
}

void fixEndianness(uint16_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (buf[i] << 8) | (buf[i] >> 8);
    }
}

void loop() {
  if (captureRequested) {
    captureRequested = false;
    
    /*
    // 1. 先读取当前光照值
    val = analogRead(2);
    mappedAEC = map(val, 0, 4095, 0, 1200);
    
    // 2. 只有当曝光值变化超过阈值时才更新
    if(abs(mappedAEC - lastAEC) > 50 ) { // 阈值设为50防止微小波动
      
      sensor_t *s = esp_camera_sensor_get();
      //int pervious_expo = s->get_aec_value(s);
      //Serial.println(pervious_expo);
      s->set_aec_value(s, mappedAEC);
      lastAEC = mappedAEC;
      
      // 3. 丢弃2帧确保新曝光生效
      for(int i=0; i<2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if(fb) esp_camera_fb_return(fb);
        delay(100);
      }
    }*/
    
    // 4. 获取最终图像
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("捕获失败");
      tft.fillScreen(TFT_BLACK);
      tft.println("捕获失败");
      Serial.println("GrabFail");
      return;
    }

    // 5. 显示图像
    
    uint16_t* processedBuffer = (uint16_t*)malloc(320*240*sizeof(uint16_t));
    memcpy(processedBuffer, fb->buf, 320*240*sizeof(uint16_t));
    //applyRGBtint(processedBuffer, 320, 240, tint);
    tft.pushImage(0, 0, 320, 240, processedBuffer);//(uint16_t *)fb->buf);
    String path = "/camera/" + String(photo_index) +".bmp";
    
    fixEndianness(processedBuffer,  320 * 240);
    writeBMP_RGB565(SD_MMC, path.c_str(), processedBuffer, 320, 240);
    photo_index = photo_index+1;
    free(processedBuffer);
    esp_camera_fb_return(fb);
    
    //Serial.printf("亮度值: %d -> 曝光值: %d\n", val, lastAEC );
  }
  delay(10);
}