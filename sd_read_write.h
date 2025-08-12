#ifndef __SD_READ_WRITE_H
#define __SD_READ_WRITE_H

#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"

#define SD_MMC_CMD  38 //Please do not modify it.
#define SD_MMC_CLK  39 //Please do not modify it. 
#define SD_MMC_D0   40 //Please do not modify it.
// Constants for BMP file format
constexpr uint16_t BMP_SIGNATURE = 0x4D42; // 'BM'
constexpr uint32_t DIB_HEADER_SIZE = 40;
constexpr uint32_t BITS_PER_PIXEL = 24;
constexpr uint32_t PIXELS_PER_METER = 2835; // ~72 DPI
constexpr size_t MAX_ROW_SIZE = 320 * 3; // Max row size for 320px width (24-bit)

// BMP文件头结构（小端序）
#pragma pack(push, 1)
typedef struct {
    uint16_t signature;     // 'BM'
    uint32_t fileSize;      // 文件总大小
    uint32_t reserved;      // 保留字段
    uint32_t dataOffset;    // 像素数据偏移（54字节）
    uint32_t dibSize;       // DIB头大小（40字节）
    int32_t  width;         // 图像宽度
    int32_t  height;        // 图像高度
    uint16_t planes;        // 颜色平面数（必须为1）
    uint16_t bpp;           // 每像素位数（24）
    uint32_t compression;   // 压缩方式（0=无压缩）
    uint32_t imageSize;     // 像素数据大小
    int32_t  xPixelsPerM;   // 水平分辨率（像素/米）
    int32_t  yPixelsPerM;   // 垂直分辨率（像素/米）
    uint32_t colorsUsed;    // 调色板颜色数（0=不使用）
    uint32_t colorsImportant; // 重要颜色数（0=全部）
} BMPHeader;
#pragma pack(pop)

void sdmmcInit(void); 

void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
void testFileIO(fs::FS &fs, const char * path);

void writejpg(fs::FS &fs, const char * path, const uint8_t *buf, size_t size);
int readFileNum(fs::FS &fs, const char * dirname);
void writebmp(fs::FS &fs, const char * path, const uint16_t *buf, size_t width, size_t height);
void writeBMP_RGB565(fs::FS &fs, const char *path, const uint16_t *rgb565Buf, size_t width, size_t height);

#endif
