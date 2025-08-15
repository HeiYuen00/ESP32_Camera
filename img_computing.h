#include <stdint.h>

typedef struct {
    uint16_t h; // Hue: 0-360 scaled to 0-255 for efficiency (~0.711 scaling)
    uint8_t s;  // Saturation: 0-255
    uint8_t v;  // Value: 0-255
} HSV;

// Convert RGB565 to HSV (optimized for ESP32)
HSV rgb565_to_hsv(uint16_t rgb) {
    HSV hsv;
    
    // Extract RGB components from RGB565 (5-6-5 format)
    uint8_t r = (rgb >> 11) & 0x1F;
    uint8_t g = (rgb >> 5) & 0x3F;
    uint8_t b = rgb & 0x1F;
    
    // Scale to 8-bit (0-255) for HSV calculation
    r = (r << 3) | (r >> 2); // Approximate 5-bit to 8-bit
    g = (g << 2) | (g >> 4); // Approximate 6-bit to 8-bit
    b = (b << 3) | (b >> 2); // Approximate 5-bit to 8-bit
    
    // Compute min, max, and delta for HSV
    uint8_t min = (r < g) ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t max = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = max - min;
    
    hsv.v = max; // Value is the maximum component
    
    if (delta == 0) {
        hsv.h = 0; // Undefined hue for achromatic colors
        hsv.s = 0; // No saturation for achromatic colors
        return hsv;
    }
    
    // Compute saturation (ensure no overflow)
    hsv.s = max ? ((uint16_t)delta * 255) / max : 0;
    
    // Compute hue (scaled to 0-255 instead of 0-360 for efficiency)
    uint16_t h;
    if (max == r) {
        h = 43 * (g - b) / delta; // ~60 degrees per region * (256/360)
    } else if (max == g) {
        h = 85 + 43 * (b - r) / delta; // 120 degrees offset
    } else {
        h = 171 + 43 * (r - g) / delta; // 240 degrees offset
    }
    
    hsv.h = h % 256; // Ensure hue stays in 0-255 range
    
    return hsv;
}

// Convert HSV back to RGB565
uint16_t hsv_to_rgb565(HSV hsv) {
    uint8_t r, g, b;
    
    if (hsv.s == 0) {
        // Achromatic: all components equal to value
        r = g = b = hsv.v;
    } else {
        // Scale hue to 0-360 for standard HSV-to-RGB conversion
        uint16_t h = (hsv.h * 360) / 255;
        
        uint8_t region = h / 60; // Divide into 6 regions
        uint8_t remainder = (h % 60) * 4; // *4 approximates /15 for efficiency
        
        // Compute intermediate values for RGB conversion
        uint8_t p = (hsv.v * (255 - hsv.s)) >> 8;
        uint8_t q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
        uint8_t t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;
        
        // Assign RGB based on hue region
        switch (region) {
            case 0: r = hsv.v; g = t; b = p; break;
            case 1: r = q; g = hsv.v; b = p; break;
            case 2: r = p; g = hsv.v; b = t; break;
            case 3: r = p; g = q; b = hsv.v; break;
            case 4: r = t; g = p; b = hsv.v; break;
            case 5: r = hsv.v; g = p; b = q; break;
            default: r = hsv.v; g = p; b = q; break; // Redundant but safe
        }
    }
    
    // Pack into RGB565 format (5-6-5)
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// In-place hue adjustment for RGB565 buffer (saves memory)
void adjust_hue_rgb565_inplace(uint16_t* buffer, 
                              uint16_t width, uint16_t height, 
                              uint8_t hue_shift) {
    // Validate inputs to prevent crashes
    if (!buffer || width == 0 || height == 0) {
        return;
    }
    
    // Process each pixel in the buffer
    for (uint32_t i = 0; i < (uint32_t)width * height; i++) {
        HSV hsv = rgb565_to_hsv(buffer[i]);
        hsv.h = (hsv.h + hue_shift) % 256; // Shift hue and wrap around
        buffer[i] = hsv_to_rgb565(hsv);
    }
}