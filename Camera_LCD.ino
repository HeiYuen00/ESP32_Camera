#include <SPI.h>
#include <TFT_eSPI.h>
#include "esp_camera.h"
#include "sd_read_write.h"
#include "img_computing.h"
#include <esp_timer.h>

#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

#define TFT_GREY 0x5AEB
#define TRIGGER_PIN 1
#define NormalMode_PIN 3
#define FilterMode_PIN 46
#define Value_PIN  2
#define DirA_PIN 14 
#define DirB_PIN 20
int val = 0;
uint8_t mappedAEC = 300;
int lastAEC = 300;
//int tint[3] = {255, 255, 255};
int photo_index = 0;
// Shared volatile flag for interrupt communication
volatile int captureRequested = 0;
volatile unsigned long lastInterruptTime = 0;
volatile unsigned long lastInterruptTime_GrabMode = 0;
volatile unsigned long lastInterruptTime_Butt = 0;
volatile bool GrabbingMode = 0;
volatile unsigned long lastUpdateExposureTime = 0;
const unsigned long debounceDelay = 200; 
const unsigned long UpdateExposureDelay = 5000; // 1000ms debounce
ColorAdjustment my_adjustments[] = {
    {HUE_BLUE, 43, 25, 15},    // 藍→紫
    {HUE_RED, 43, 20, 5},      // 紅→黃
    {HUE_GREEN, -20, 15, 50}   // 綠微調
};


TFT_eSPI tft = TFT_eSPI();


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
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while(1); // Halt if camera fails
  }

  sensor_t *s = esp_camera_sensor_get();

  // Basic camera settings
  s->set_vflip(s, 1);        // Vertical flip (if needed)
  s->set_brightness(s, 2);   // Brightness (0 = default)
  s->set_contrast(s, 0);     // Contrast (0 = default, 1 = slight increase)
  s->set_saturation(s, 0);   // Saturation (0 = default)
  s->set_special_effect(s, 0); // No special effect

  // Auto White Balance (AWB)
  s->set_wb_mode(s, 0);      // 0 = Auto White Balance

  // Auto Exposure Control (AEC) - Critical for auto exposure
  s->set_exposure_ctrl(s, 1); // 
  s->set_aec2(s, 1);         // Enable AEC2 (more advanced auto exposure)
  s->set_ae_level(s, -2);     // AE level (0 = default, -2 to +2 for adjustments)

  // Auto Gain Control (AGC) - Often needed with auto exposure
  s->set_gain_ctrl(s, 1);    // Enable automatic gain control
  s->set_agc_gain(s, 0);     // Auto gain (0 = full auto)

  // 关键曝光控制设置
  //s->set_exposure_ctrl(s, 0);   // 启用手动曝光
  //s->set_aec2(s, 1);            // 禁用AEC2
  //s->set_gain_ctrl(s, 0);       // 禁用自动增益控制
  //s->set_agc_gain(s, 0);        // 固定增益值
  //s->set_aec_value(s, mappedAEC);
  /* 
  ov5640.start(s);

  if (ov5640.focusInit() == 0) {
    Serial.println("OV5640_Focus_Init Successful!");
  }

  if (ov5640.autoFocusMode() == 0) {
    Serial.println("OV5640_Auto_Focus Successful!");
  }
  */
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

  // Setup Grabbing interrupt
  pinMode(TRIGGER_PIN, INPUT);
  pinMode(NormalMode_PIN, INPUT);
  pinMode(FilterMode_PIN, INPUT);
  attachInterrupt(TRIGGER_PIN, GrabOneImg, FALLING);
  attachInterrupt(NormalMode_PIN, NormalGrabMode_EN, RISING);
  attachInterrupt(FilterMode_PIN, NormalGrabMode_DIS, RISING);
  //Setup Direction Button
  pinMode(DirA_PIN, INPUT);
  pinMode(DirB_PIN, INPUT);
  Serial.println("DoneSetupInputPIN");
  attachInterrupt(DirA_PIN, TriggerA, FALLING);
  attachInterrupt(DirB_PIN, TriggerB, FALLING);
  //Done
  
  initGrabMode();
  Serial.println("System Ready");
}

//Grabbing Mode Control
void initGrabMode(){
  GrabbingMode = digitalRead(NormalMode_PIN);
  Serial.println(GrabbingMode);
}

void NormalGrabMode(bool Normal_EN){
  GrabbingMode = Normal_EN;
  Serial.println(GrabbingMode);
}

void NormalGrabMode_EN(){
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime_GrabMode > debounceDelay) {
    NormalGrabMode(1);
  }
  lastInterruptTime_GrabMode = currentTime;
}

void NormalGrabMode_DIS(){
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime_GrabMode > debounceDelay) {
    NormalGrabMode(0);
  }
  lastInterruptTime_GrabMode = currentTime;
}
//
void GrabOneImg() {
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime > debounceDelay) {
    //Serial.println(captureRequested);
    if(captureRequested == 2){
      captureRequested = 0;
    }
    else{
      captureRequested = 1;
    }
    //Serial.println(captureRequested);
    lastInterruptTime = currentTime;
  }
}
//

//5 dir input
void checktrigger(int trigger){
  Serial.println(trigger);
}

void TriggerA(){
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime_Butt > debounceDelay) {
    checktrigger(1);
  }
  lastInterruptTime_Butt = currentTime;
}

void TriggerB(){
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime_Butt > debounceDelay) {
    checktrigger(2);
  }
  lastInterruptTime_Butt = currentTime;
}
//

//Fix Image
void fixEndianness(uint16_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (buf[i] << 8) | (buf[i] >> 8);
    }
}

void fixEndianness_fast(uint16_t *buf, size_t len) {
    uint32_t *buf32 = (uint32_t *)buf;
    size_t len32 = len / 2;
    
    // Process 2 pixels at a time
    for(size_t i = 0; i < len32; i++) {
        uint32_t val = buf32[i];
        buf32[i] = ((val & 0xFF00FF00) >> 8) | ((val & 0x00FF00FF) << 8);
    }
    
    // Handle remaining odd pixel if exists
    if(len & 1) {
        buf[len-1] = __builtin_bswap16(buf[len-1]);
    }
}
//

void loop() {
  if(captureRequested != 2)
  {
    /*
    uint8_t rc = ov5640.getFWStatus();
    Serial.printf("FW_STATUS = 0x%x\n", rc);

    if (rc == -1) {
      Serial.println("Check your OV5640");
    } else if (rc == FW_STATUS_S_FOCUSED) {
      Serial.println("Focused!");
    } else if (rc == FW_STATUS_S_FOCUSING) {
      Serial.println("Focusing!");
    } else {
    }
    */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("捕获失败");
      tft.fillScreen(TFT_BLACK);
      tft.println("捕获失败");
      Serial.println("GrabFail");
      return;
    }

    uint16_t* processedBuffer = (uint16_t*)malloc(320*240*sizeof(uint16_t));
    Serial.println("Memory Pool Setup Done");
    memcpy(processedBuffer, fb->buf, 320*240*sizeof(uint16_t));
    String path = "/camera/" + String(photo_index) +".bmp";

    if(GrabbingMode == 1)
    {
      //Serial.println("Update image");
      tft.pushImage(0, 0, 320, 240, processedBuffer);
      if (captureRequested == 1) {
          captureRequested = 2;
          fixEndianness_fast(processedBuffer,  320 * 240);
          writeBMP_RGB565(SD_MMC, path.c_str(), processedBuffer, 320, 240);    
          photo_index = photo_index+1;
      }
    }
    else{
      val = analogRead(2);
      mappedAEC = map(val, 0, 4095, -330, 30);
      fixEndianness_fast(processedBuffer,  320 * 240);
      uint64_t Starttime = esp_timer_get_time();
      //adjust_hue_rgb565_inplace(processedBuffer,320,240,mappedAEC);
      //adjust_hue_rgb565_parallel(processedBuffer,320*240,mappedAEC);
      //process_noisy_image(processedBuffer, 320, 240);
      adjust_multiple_colors_parallel(processedBuffer, 320*240, my_adjustments, 3);
      uint64_t Finishtime = esp_timer_get_time();
      
      if (captureRequested == 1) {
        Serial.println(Finishtime - Starttime);
        captureRequested = 2;
        writeBMP_RGB565(SD_MMC, path.c_str(), processedBuffer, 320, 240);
        photo_index = photo_index+1;
      }
      fixEndianness_fast(processedBuffer,  320 * 240);
      tft.pushImage(0, 0, 320, 240, processedBuffer);

    }
    free(processedBuffer);

  esp_camera_fb_return(fb);
  }
  
  delay(10);
}