#include "sd_read_write.h"
#include <FS.h>
#include <Arduino.h>




void sdmmcInit(void){
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
      Serial.println("No SD_MMC card attached");
      return;
  }
  Serial.print("SD_MMC Card Type: ");
  if(cardType == CARD_MMC){
      Serial.println("MMC");
  } else if(cardType == CARD_SD){
      Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
      Serial.println("SDHC");
  } else {
      Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);  
  Serial.printf("Total space: %lluMB\r\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\r\n", SD_MMC.usedBytes() / (1024 * 1024));
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void createDir(fs::FS &fs, const char * path){
    Serial.printf("Creating Dir: %s\n", path);
    if(fs.mkdir(path)){
        Serial.println("Dir created");
    } else {
        Serial.println("mkdir failed");
    }
}

void removeDir(fs::FS &fs, const char * path){
    Serial.printf("Removing Dir: %s\n", path);
    if(fs.rmdir(path)){
        Serial.println("Dir removed");
    } else {
        Serial.println("rmdir failed");
    }
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("File renamed");
    } else {
        Serial.println("Rename failed");
    }
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\n", path);
    if(fs.remove(path)){
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}

void testFileIO(fs::FS &fs, const char * path){
    File file = fs.open(path);
    static uint8_t buf[512];
    size_t len = 0;
    uint32_t start = millis();
    uint32_t end = start;
    if(file){
        len = file.size();
        size_t flen = len;
        start = millis();
        while(len){
            size_t toRead = len;
            if(toRead > 512){
                toRead = 512;
            }
            file.read(buf, toRead);
            len -= toRead;
        }
        end = millis() - start;
        Serial.printf("%u bytes read for %u ms\r\n", flen, end);
        file.close();
    } else {
        Serial.println("Failed to open file for reading");
    }

    file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }

    size_t i;
    start = millis();
    for(i=0; i<2048; i++){
        file.write(buf, 512);
    }
    end = millis() - start;
    Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
    file.close();
}

void writejpg(fs::FS &fs, const char * path, const uint8_t *buf, size_t size){
    File file = fs.open(path, FILE_WRITE);
    if(!file){
      Serial.println("Failed to open file for writing");
      return;
    }
    file.write(buf, size);
    Serial.printf("Saved file to path: %s\r\n", path);
}

int readFileNum(fs::FS &fs, const char * dirname){
    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return -1;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return -1;
    }

    File file = root.openNextFile();
    int num=0;
    while(file){
      file = root.openNextFile();
      num++;
    }
    return num;  
}

void writebmp(fs::FS &fs, const char *path, const uint16_t *buf, size_t width, size_t height) {
    // Input validation
    if (!path || !buf || width == 0 || height == 0) {
        Serial.println("Invalid parameters: path, buffer, or dimensions are null/zero");
        return;
    }
    if (width != 320 || height != 240) {
        Serial.printf("Unsupported dimensions: %dx%d (expected 320x240)\n", width, height);
        return;
    }

    // Open file for writing
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("Failed to open file for writing: %s\n", path);
        return;
    }

    // Calculate row padding (BMP rows must be multiple of 4 bytes)
    size_t bytes_per_row = width * 3;
    size_t padding_bytes = (4 - (bytes_per_row % 4)) % 4;
    size_t padded_row_size = bytes_per_row + padding_bytes;
    size_t pixel_data_size = padded_row_size * height;
    uint32_t file_size = 54 + pixel_data_size; // 14 (BMP header) + 40 (DIB header) + pixel data

    // BMP file header (14 bytes)
    uint32_t reserved = 0;
    uint32_t pixel_data_offset = 54;

    if (!file.write((uint8_t*)&BMP_SIGNATURE, 2) ||
        !file.write((uint8_t*)&file_size, 4) ||
        !file.write((uint8_t*)&reserved, 4) ||
        !file.write((uint8_t*)&pixel_data_offset, 4)) {
        Serial.println("Failed to write BMP header");
        file.close();
        return;
    }

    // DIB header (40 bytes)
    uint32_t planes = 1;
    uint32_t compression = 0;
    uint32_t image_size = pixel_data_size;
    int32_t signed_width = static_cast<int32_t>(width);
    int32_t signed_height = static_cast<int32_t>(height);

    if (!file.write((uint8_t*)&DIB_HEADER_SIZE, 4) ||
        !file.write((uint8_t*)&signed_width, 4) ||
        !file.write((uint8_t*)&signed_height, 4) ||
        !file.write((uint8_t*)&planes, 2) ||
        !file.write((uint8_t*)&BITS_PER_PIXEL, 2) ||
        !file.write((uint8_t*)&compression, 4) ||
        !file.write((uint8_t*)&image_size, 4) ||
        !file.write((uint8_t*)&PIXELS_PER_METER, 4) ||
        !file.write((uint8_t*)&PIXELS_PER_METER, 4) ||
        !file.write((uint8_t*)&reserved, 4) || // colorsUsed
        !file.write((uint8_t*)&reserved, 4)) { // importantColors
        Serial.println("Failed to write DIB header");
        file.close();
        return;
    }

    // Convert and write pixel data row by row (RGB565 to 24-bit BGR)
    uint8_t row_buffer[MAX_ROW_SIZE + 3]; // Max row size + max padding (3 bytes)
    uint8_t padding[3] = {0, 0, 0}; // Padding bytes (up to 3)

    for (int32_t y = height - 1; y >= 0; y--) {
        size_t buffer_index = 0;
        for (size_t x = 0; x < width; x++) {
            uint16_t rgb565 = buf[y * width + x];

            // Correct RGB565 to 24-bit BGR conversion
            // Extract 5-6-5 components
            uint8_t r = (rgb565 >> 11) & 0x1F;  // 5 bits (red)
            uint8_t g = (rgb565 >> 5)  & 0x3F;  // 6 bits (green)
            uint8_t b = rgb565 & 0x1F;          // 5 bits (blue)

            // Scale to 8-bit (shift left and OR with top bits for smoother gradient)
            r = (r << 3) | (r >> 2);   // 5 → 8 bits (e.g., 0x1F → 0xFF)
            g = (g << 2) | (g >> 4);   // 6 → 8 bits (e.g., 0x3F → 0xFF)
            b = (b << 3) | (b >> 2);   // 5 → 8 bits (e.g., 0x1F → 0xFF)

            // Store in BGR order (BMP format)
            row_buffer[buffer_index++] = b;
            row_buffer[buffer_index++] = g;
            row_buffer[buffer_index++] = r;
        }

        // Add padding bytes
        for (size_t i = 0; i < padding_bytes; i++) {
            row_buffer[buffer_index++] = 0;
        }

        // Write the entire row
        if (!file.write(row_buffer, padded_row_size)) {
            Serial.println("Failed to write pixel data");
            file.close();
            return;
        }
    }

    file.close();
    Serial.printf("Saved %dx%d RGB565 BMP to: %s\n", width, height, path);
}


void writeBMP_RGB565(fs::FS &fs, const char *path, const uint16_t *rgb565Buf, size_t width, size_t height) {
    // 1. 打开文件
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file");
        return;
    }

    // 2. 计算BMP行对齐
    size_t bytesPerRow = width * 3;
    size_t padding = (4 - (bytesPerRow % 4)) % 4;
    size_t rowSize = bytesPerRow + padding;

    // 3. 写入BMP头（与纯色测试相同，确保头正确）
    BMPHeader header = {
        .signature = 0x4D42,
        .fileSize = 54 + rowSize * height,
        .dataOffset = 54,
        .dibSize = 40,
        .width = static_cast<int32_t>(width),
        .height = static_cast<int32_t>(height),
        .planes = 1,
        .bpp = 24,
        .compression = 0,
        .imageSize = rowSize * height,
        .xPixelsPerM = 2835,
        .yPixelsPerM = 2835
    };
    file.write((uint8_t*)&header, sizeof(BMPHeader));

    // 4. 转换并写入像素数据
    uint8_t rowBuffer[rowSize];
    for (int32_t y = 0; y < height; y++) { // BMP从底部开始存储
        size_t bufIndex = 0;
        for (size_t x = 0; x < width; x++) {
            uint16_t pixel = rgb565Buf[y * width + x];
            
            // 正确提取RGB565分量
            uint8_t r = (pixel >> 11) & 0x1F; // 高5位红色
            uint8_t g = (pixel >> 5)  & 0x3F; // 中6位绿色
            uint8_t b = pixel & 0x1F;         // 低5位蓝色

            // 扩展到8位（关键修复！）
            r = (r << 3) | (r >> 2); // 5位→8位
            g = (g << 2) | (g >> 4); // 6位→8位
            b = (b << 3) | (b >> 2); // 5位→8位

            // 按BGR顺序存储
            rowBuffer[bufIndex++] = b;
            rowBuffer[bufIndex++] = g;
            rowBuffer[bufIndex++] = r;
        }
        // 写入行数据+填充
        file.write(rowBuffer, rowSize);
    }
    file.close();
    Serial.printf("Saved: %s\n", path);
}