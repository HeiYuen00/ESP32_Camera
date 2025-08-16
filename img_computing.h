#include <stdint.h>
#include "esp32-hal.h" // For ESP32-S3 specific optimizations

// Use PROGMEM for lookup tables to keep them in flash
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

// Packed HSV structure for better cache utilization
typedef struct __attribute__((packed)) {
    uint16_t h; // 0-255 (scaled from 0-360)
    uint8_t s;  // 0-255
    uint8_t v;  // 0-255
} HSV;

// Vectorized RGB565 to HSV conversion
__attribute__((always_inline)) inline HSV rgb565_to_hsv(uint16_t rgb) {
    // ESP32-S3 has faster flash access, so we can use PROGMEM directly
    uint8_t r = five_to_eight[(rgb >> 11) & 0x1F];
    uint8_t g = six_to_eight[(rgb >> 5) & 0x3F];
    uint8_t b = five_to_eight[rgb & 0x1F];
    
    // Find min/max using branchless techniques
    uint8_t min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = max - min;
    
    HSV hsv = {0, 0, max};
    
    if (delta) {
        // Fast saturation calculation using multiply-shift
        hsv.s = ((uint16_t)delta * 255) / max;
        
        // Branchless hue calculation
        uint16_t h = (max == r) ? (43 * (g - b) / delta) :
                    (max == g) ? (85 + 43 * (b - r) / delta) :
                                (171 + 43 * (r - g) / delta);
        hsv.h = h & 0xFF;
    }
    
    return hsv;
}

// Optimized HSV to RGB565 with ESP32-S3 vector potential
__attribute__((always_inline)) inline uint16_t hsv_to_rgb565(HSV hsv) {
    if (hsv.s == 0) {
        uint8_t v = hsv.v >> 3;
        return (v << 11) | (v << 5) | v;
    }
    
    // Optimized HSV conversion using fixed-point math
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
    
    // ESP32-S3 has faster bitfield operations
    return (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3;
}

// Check if color is in specific HSV range
bool is_in_hue_range(HSV hsv, uint8_t target_hue, uint8_t range = 15) {
    uint8_t diff = abs(hsv.h - target_hue);
    return diff < range || (255 - diff) < range;
}

struct HueAdjustParams_hug {
    uint16_t* buf;
    uint32_t len;
    uint8_t target_hue;
    uint8_t hue_shift;
    uint8_t range;
};

void adjust_specific_hue_parallel(uint16_t* buffer, uint32_t pixel_count,
                                uint8_t target_hue, uint8_t hue_shift,
                                uint8_t range = 15) {
    TaskHandle_t task_handle;
    HueAdjustParams_hug* params = new HueAdjustParams_hug{
        buffer + pixel_count/2,
        pixel_count - pixel_count/2,
        target_hue,
        hue_shift,
        range
    };

    auto task_func = [](void* p) {
        HueAdjustParams_hug* args = (HueAdjustParams_hug*)p;
        for (uint32_t i = 0; i < args->len; i++) {
            HSV hsv = rgb565_to_hsv(args->buf[i]);
            if (is_in_hue_range(hsv, args->target_hue, args->range)) {
                hsv.h = (hsv.h + args->hue_shift) & 0xFF;
                args->buf[i] = hsv_to_rgb565(hsv);
            }
        }
        delete args;
        vTaskDelete(NULL);
    };

    xTaskCreatePinnedToCore(
        task_func,
        "hue_task",
        2048,
        params,
        1,
        &task_handle,
        !xPortGetCoreID()
    );

    // Process first half
    uint32_t half_count = pixel_count/2;
    for (uint32_t i = 0; i < half_count; i++) {
        HSV hsv = rgb565_to_hsv(buffer[i]);
        if (is_in_hue_range(hsv, target_hue, range)) {
            hsv.h = (hsv.h + hue_shift) & 0xFF;
            buffer[i] = hsv_to_rgb565(hsv);
        }
    }

    while(eTaskGetState(task_handle) != eDeleted) {}
}


struct HueAdjustParams {
    uint16_t* buf;
    uint32_t len;
    uint8_t shift;
};

// Helper function for PSRAM processing
void process_chunk(uint16_t* chunk, uint32_t count, uint8_t hue_shift) {
    uint16_t* end = chunk + count;
    while(chunk < end) {
        HSV hsv = rgb565_to_hsv(*chunk);
        hsv.h = (hsv.h + hue_shift) & 0xFF;
        *chunk++ = hsv_to_rgb565(hsv);
    }
}

// Then modify the parallel processing function
// Modified parallel processing function with memory optimizations
void adjust_hue_rgb565_parallel(uint16_t* buffer, uint32_t pixel_count, uint8_t hue_shift) {
    // Check if buffer is in PSRAM (slower access)
    bool is_psram = (uint32_t)buffer >= 0x3F800000;
    
    if(is_psram) {
        // If using PSRAM, process in chunks to minimize cache misses
        const uint16_t chunk_size = 256;  // Fits in cache
        for(uint32_t i = 0; i < pixel_count; i += chunk_size) {
            uint32_t end = min(i + chunk_size, pixel_count);
            process_chunk(buffer + i, end - i, hue_shift);
        }
        return;
    }

    // Normal processing for internal RAM
    TaskHandle_t task_handle;
    HueAdjustParams* params = new HueAdjustParams{
        buffer + pixel_count/2, 
        pixel_count - pixel_count/2, 
        hue_shift
    };

    xTaskCreatePinnedToCore(
        [](void* p) {
            HueAdjustParams* args = (HueAdjustParams*)p;
            uint16_t* end = args->buf + args->len;
            while(args->buf < end) {  // Pointer arithmetic faster than indexing
                HSV hsv = rgb565_to_hsv(*args->buf);
                hsv.h = (hsv.h + args->shift) & 0xFF;
                *args->buf++ = hsv_to_rgb565(hsv);
            }
            delete args;
            vTaskDelete(NULL);
        },
        "hue_task",
        2048,
        params,
        1,
        &task_handle,
        !xPortGetCoreID()
    );

    // Process first half with same optimization
    uint16_t* end = buffer + pixel_count/2;
    while(buffer < end) {
        HSV hsv = rgb565_to_hsv(*buffer);
        hsv.h = (hsv.h + hue_shift) & 0xFF;
        *buffer++ = hsv_to_rgb565(hsv);
    }

    while(eTaskGetState(task_handle) != eDeleted) {}
}

