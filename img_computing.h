#include <stdint.h>
#include "esp32-hal.h"
#include <Arduino.h>
#include "esp_task_wdt.h"

// 查表法轉換表格 (PROGMEM 存儲在Flash中)
static const uint8_t five_to_eight[] PROGMEM = {
    0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123,
    132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255
};

static const uint8_t six_to_eight[] PROGMEM = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
    128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172, 176, 180, 184, 188,
    192, 196, 200, 204, 208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 248, 252
};

// HSV 色彩結構
typedef struct __attribute__((packed)) {
    uint16_t h; // 0-255 (從 0-360 縮放)
    uint8_t s;  // 0-255
    uint8_t v;  // 0-255
} HSV;

// 顏色調整參數結構
struct ColorAdjustment {
    uint8_t target_hue;    // 目標色調 (0-255)
    int8_t hue_shift;      // 色調偏移量 (-255 to +255)
    uint8_t range;         // 檢測範圍 (0-128)
    int8_t sat_shift;      // 飽和度調整 (-255 to +255)
};

// 常用顏色定義 (0-255 色調值)
#define HUE_RED       0
#define HUE_ORANGE    20
#define HUE_YELLOW    43
#define HUE_GREEN     85
#define HUE_CYAN      128
#define HUE_BLUE      170
#define HUE_PURPLE    213
#define HUE_PINK      234

// ==================== 核心轉換函數 ====================

// RGB565 轉 HSV (優化版本)
__attribute__((always_inline)) inline HSV IRAM_ATTR rgb565_to_hsv(uint16_t rgb) {
    HSV hsv;
    
    // 提取並轉換 RGB 分量
    uint8_t r = five_to_eight[(rgb >> 11) & 0x1F];
    uint8_t g = six_to_eight[(rgb >> 5) & 0x3F];
    uint8_t b = five_to_eight[rgb & 0x1F];
    
    // 計算最小值、最大值和差值
    uint8_t min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = max - min;
    
    hsv.v = max;
    
    if (delta == 0) {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }
    
    // 計算飽和度
    hsv.s = ((uint16_t)delta * 255) / max;
    
    // 計算色調
    uint16_t h;
    if (max == r) {
        h = 43 * (g - b) / delta;
    } else if (max == g) {
        h = 85 + 43 * (b - r) / delta;
    } else {
        h = 171 + 43 * (r - g) / delta;
    }
    
    hsv.h = h & 0xFF;
    return hsv;
}

// HSV 轉 RGB565 (優化版本)
__attribute__((always_inline)) inline uint16_t IRAM_ATTR hsv_to_rgb565(HSV hsv) {
    if (hsv.s == 0) {
        uint8_t v = hsv.v >> 3;
        return (v << 11) | (v << 5) | v;
    }
    
    uint8_t region = hsv.h / 43;
    uint8_t rem = (hsv.h % 43) * 6;
    
    uint8_t p = (hsv.v * (255 - hsv.s)) >> 8;
    uint8_t q = (hsv.v * (255 - ((hsv.s * rem) >> 8))) >> 8;
    uint8_t t = (hsv.v * (255 - ((hsv.s * (255 - rem)) >> 8))) >> 8;
    
    uint8_t r, g, b;
    switch (region) {
        case 0: r = hsv.v; g = t; b = p; break;
        case 1: r = q; g = hsv.v; b = p; break;
        case 2: r = p; g = hsv.v; b = t; break;
        case 3: r = p; g = q; b = hsv.v; break;
        case 4: r = t; g = p; b = hsv.v; break;
        default: r = hsv.v; g = p; b = q; break;
    }
    
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ==================== 平行處理結構 ====================

struct MultiColorParams {
    uint16_t* buf;
    uint32_t len;
    ColorAdjustment* adjustments;
    uint8_t num_adjustments;
};

// ==================== 多重顏色平行處理 ====================

void IRAM_ATTR adjust_multiple_colors_parallel(uint16_t* buffer, uint32_t pixel_count, 
                                             ColorAdjustment* adjustments, uint8_t num_adjustments) {
    if (num_adjustments == 0 || buffer == nullptr) return;
    
    TaskHandle_t task_handle = nullptr;
    
    // 為第二核心建立參數
    MultiColorParams* params = new MultiColorParams();
    params->buf = buffer + pixel_count / 2;
    params->len = pixel_count - pixel_count / 2;
    params->adjustments = adjustments;
    params->num_adjustments = num_adjustments;

    // 第二核心任務函數
    auto task_func = [](void* p) {
        MultiColorParams* args = (MultiColorParams*)p;
        
        for (uint32_t i = 0; i < args->len; i++) {
            HSV hsv = rgb565_to_hsv(args->buf[i]);
            bool adjusted = false;
            
            for (uint8_t j = 0; j < args->num_adjustments && !adjusted; j++) {
                ColorAdjustment adj = args->adjustments[j];
                uint8_t diff = abs(hsv.h - adj.target_hue);
                
                // 檢查色調範圍
                if (diff <= adj.range || (255 - diff) <= adj.range) {
                    // 應用色調調整
                    int32_t new_hue = hsv.h + adj.hue_shift;
                    if (new_hue < 0) new_hue += 256;
                    hsv.h = new_hue & 0xFF;
                    
                    // 應用飽和度調整
                    if (adj.sat_shift > 0) {
                        hsv.s = min(255, hsv.s + adj.sat_shift);
                    } else if (adj.sat_shift < 0) {
                        hsv.s = max(0, hsv.s - abs(adj.sat_shift));
                    }
                    
                    args->buf[i] = hsv_to_rgb565(hsv);
                    adjusted = true;
                }
            }
        }
        
        delete args;
        vTaskDelete(NULL);
    };

    // 在第二核心建立任務
    BaseType_t result = xTaskCreatePinnedToCore(
        task_func,
        "multi_color_task",
        4096,  // 堆疊大小
        params,
        1,     // 優先級
        &task_handle,
        !xPortGetCoreID()  // 在另一個核心運行
    );

    if (result != pdPASS) {
        Serial.println("Failed to create task! Falling back to single core.");
        delete params;
        // 單核心備援處理
        for (uint32_t i = 0; i < pixel_count; i++) {
            HSV hsv = rgb565_to_hsv(buffer[i]);
            bool adjusted = false;
            
            for (uint8_t j = 0; j < num_adjustments && !adjusted; j++) {
                ColorAdjustment adj = adjustments[j];
                uint8_t diff = abs(hsv.h - adj.target_hue);
                
                if (diff <= adj.range || (255 - diff) <= adj.range) {
                    int32_t new_hue = hsv.h + adj.hue_shift;
                    if (new_hue < 0) new_hue += 256;
                    hsv.h = new_hue & 0xFF;
                    
                    if (adj.sat_shift > 0) {
                        hsv.s = min(255, hsv.s + adj.sat_shift);
                    } else if (adj.sat_shift < 0) {
                        hsv.s = max(0, hsv.s - abs(adj.sat_shift));
                    }
                    
                    buffer[i] = hsv_to_rgb565(hsv);
                    adjusted = true;
                }
            }
        }
        return;
    }

    // 第一核心處理前半部分
    uint32_t half_count = pixel_count / 2;
    for (uint32_t i = 0; i < half_count; i++) {
        HSV hsv = rgb565_to_hsv(buffer[i]);
        bool adjusted = false;
        
        for (uint8_t j = 0; j < num_adjustments && !adjusted; j++) {
            ColorAdjustment adj = adjustments[j];
            uint8_t diff = abs(hsv.h - adj.target_hue);
            
            if (diff <= adj.range || (255 - diff) <= adj.range) {
                int32_t new_hue = hsv.h + adj.hue_shift;
                if (new_hue < 0) new_hue += 256;
                hsv.h = new_hue & 0xFF;
                
                if (adj.sat_shift > 0) {
                    hsv.s = min(255, hsv.s + adj.sat_shift);
                } else if (adj.sat_shift < 0) {
                    hsv.s = max(0, hsv.s - abs(adj.sat_shift));
                }
                
                buffer[i] = hsv_to_rgb565(hsv);
                adjusted = true;
            }
        }
    }

    // 等待第二核心任務完成
    if (task_handle != nullptr) {
        while (eTaskGetState(task_handle) != eDeleted) {
            delay(1);
        }
    }
}

// void applyRGBtint(uint16_t* imageBuffer, int width, int height, const int rgbTint[3]) {
//     // Extract tint components (0-255)
//     int rTint = rgbTint[0];
//     int gTint = rgbTint[1];
//     int bTint = rgbTint[2];

//     // Normalize tint factors (0.0-1.0)
//     float rFactor = rTint / 255.0f;
//     float gFactor = gTint / 255.0f;
//     float bFactor = bTint / 255.0f;

//     for (int y = 0; y < height; y++) {
//         for (int x = 0; x < width; x++) {
//             // Get current pixel index
//             int index = y * width + x;
            
//             // Extract RGB565 components
//             uint16_t pixel = imageBuffer[index];
//             uint8_t r = (pixel >> 11) & 0x1F;  // 5 bits
//             uint8_t g = (pixel >> 5)  & 0x3F;   // 6 bits
//             uint8_t b = pixel & 0x1F;           // 5 bits

//             // Convert to 8-bit (for better tint calculation)
//             r = (r << 3) | (r >> 2);  // 5->8 bits
//             g = (g << 2) | (g >> 4);  // 6->8 bits
//             b = (b << 3) | (b >> 2);  // 5->8 bits

//             // Apply tint (multiplicative blending)
//             r = static_cast<uint8_t>(r * rFactor);
//             g = static_cast<uint8_t>(g * gFactor);
//             b = static_cast<uint8_t>(b * bFactor);

//             // Convert back to RGB565
//             r = r >> 3;  // 8->5 bits
//             g = g >> 2;  // 8->6 bits
//             b = b >> 3;  // 8->5 bits

//             // Recombine into 16-bit pixel
//             imageBuffer[index] = (r << 11) | (g << 5) | b;
//         }
//     }
// }

