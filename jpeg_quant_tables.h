#ifndef JPEG_QUANT_TABLES_H_
#define JPEG_QUANT_TABLES_H_

#include <stdint.h>

namespace pik {

static const int kNumStockQuantTables = 8;
static const uint8_t
    kStockQuantizationTables[2][kNumStockQuantTables][64] = {
  {  // LUMA
    {  3,  2,  2,  3,  5,  8, 10, 12,
       2,  2,  3,  4,  5, 12, 12, 11,
       3,  3,  3,  5,  8, 11, 14, 11,
       3,  3,  4,  6, 10, 17, 16, 12,
       4,  4,  7, 11, 14, 22, 21, 15,
       5,  7, 11, 13, 16, 21, 23, 18,
      10, 13, 16, 17, 21, 24, 24, 20,
      14, 18, 19, 20, 22, 20, 21, 20 },
    {  8,  6,  5,  8, 12, 20, 26, 31,
       6,  6,  7, 10, 13, 29, 30, 28,
       7,  7,  8, 12, 20, 29, 35, 28,
       7,  9, 11, 15, 26, 44, 40, 31,
       9, 11, 19, 28, 34, 55, 52, 39,
      12, 18, 28, 32, 41, 52, 57, 46,
      25, 32, 39, 44, 52, 61, 60, 51,
      36, 46, 48, 49, 56, 50, 52, 50 },
    {  6,  4,  4,  6, 10, 16, 20, 24,
       5,  5,  6,  8, 10, 23, 24, 22,
       6,  5,  6, 10, 16, 23, 28, 22,
       6,  7,  9, 12, 20, 35, 32, 25,
       7,  9, 15, 22, 27, 44, 41, 31,
      10, 14, 22, 26, 32, 42, 45, 37,
      20, 26, 31, 35, 41, 48, 48, 40,
      29, 37, 38, 39, 45, 40, 41, 40 },
    {  5,  3,  3,  5,  7, 12, 15, 18,
       4,  4,  4,  6,  8, 17, 18, 17,
       4,  4,  5,  7, 12, 17, 21, 17,
       4,  5,  7,  9, 15, 26, 24, 19,
       5,  7, 11, 17, 20, 33, 31, 23,
       7, 11, 17, 19, 24, 31, 34, 28,
      15, 19, 23, 26, 31, 36, 36, 30,
      22, 28, 29, 29, 34, 30, 31, 30 },
    {  1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1 },
    {  2,  1,  1,  2,  2,  4,  5,  6,
       1,  1,  1,  2,  3,  6,  6,  6,
       1,  1,  2,  2,  4,  6,  7,  6,
       1,  2,  2,  3,  5,  9,  8,  6,
       2,  2,  4,  6,  7, 11, 10,  8,
       2,  4,  6,  6,  8, 10, 11,  9,
       5,  6,  8,  9, 10, 12, 12, 10,
       7,  9, 10, 10, 11, 10, 10, 10 },
    {  1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  2,
       1,  1,  1,  1,  1,  1,  2,  2,
       1,  1,  1,  1,  1,  2,  2,  3,
       1,  1,  1,  1,  2,  2,  3,  3,
       1,  1,  1,  2,  2,  3,  3,  3,
       1,  1,  2,  2,  3,  3,  3,  3 },
    { 10,  7,  6, 10, 14, 24, 31, 37,
       7,  7,  8, 11, 16, 35, 36, 33,
       8,  8, 10, 14, 24, 34, 41, 34,
       8, 10, 13, 17, 31, 52, 48, 37,
      11, 13, 22, 34, 41, 65, 62, 46,
      14, 21, 33, 38, 49, 62, 68, 55,
      29, 38, 47, 52, 62, 73, 72, 61,
      43, 55, 57, 59, 67, 60, 62, 59 }
  },
  {   // CHROMA
    {  9,  9,  9, 12, 11, 12, 24, 13,
      13, 24, 50, 33, 28, 33, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50 },
    {  3,  4,  5,  9, 20, 20, 20, 20,
       4,  4,  5, 13, 20, 20, 20, 20,
       5,  5, 11, 20, 20, 20, 20, 20,
       9, 13, 20, 20, 20, 20, 20, 20,
      20, 20, 20, 20, 20, 20, 20, 20,
      20, 20, 20, 20, 20, 20, 20, 20,
      20, 20, 20, 20, 20, 20, 20, 20,
      20, 20, 20, 20, 20, 20, 20, 20 },
    {  9,  9, 12, 24, 50, 50, 50, 50,
       9, 11, 13, 33, 50, 50, 50, 50,
      12, 13, 28, 50, 50, 50, 50, 50,
      24, 33, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50,
      50, 50, 50, 50, 50, 50, 50, 50 },
    {  5,  5,  7, 14, 30, 30, 30, 30,
       5,  6,  8, 20, 30, 30, 30, 30,
       7,  8, 17, 30, 30, 30, 30, 30,
      14, 20, 30, 30, 30, 30, 30, 30,
      30, 30, 30, 30, 30, 30, 30, 30,
      30, 30, 30, 30, 30, 30, 30, 30,
      30, 30, 30, 30, 30, 30, 30, 30,
      30, 30, 30, 30, 30, 30, 30, 30 },
    {  7,  7, 10, 19, 40, 40, 40, 40,
       7,  8, 10, 26, 40, 40, 40, 40,
      10, 10, 22, 40, 40, 40, 40, 40,
      19, 26, 40, 40, 40, 40, 40, 40,
      40, 40, 40, 40, 40, 40, 40, 40,
      40, 40, 40, 40, 40, 40, 40, 40,
      40, 40, 40, 40, 40, 40, 40, 40,
      40, 40, 40, 40, 40, 40, 40, 40 },
    {  1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1,
       1,  1,  1,  1,  1,  1,  1,  1 },
    {  2,  2,  2,  5, 10, 10, 10, 10,
       2,  2,  3,  7, 10, 10, 10, 10,
       2,  3,  6, 10, 10, 10, 10, 10,
       5,  7, 10, 10, 10, 10, 10, 10,
      10, 10, 10, 10, 10, 10, 10, 10,
      10, 10, 10, 10, 10, 10, 10, 10,
      10, 10, 10, 10, 10, 10, 10, 10,
      10, 10, 10, 10, 10, 10, 10, 10 },
    { 10, 11, 14, 28, 59, 59, 59, 59,
      11, 13, 16, 40, 59, 59, 59, 59,
      14, 16, 34, 59, 59, 59, 59, 59,
      28, 40, 59, 59, 59, 59, 59, 59,
      59, 59, 59, 59, 59, 59, 59, 59,
      59, 59, 59, 59, 59, 59, 59, 59,
      59, 59, 59, 59, 59, 59, 59, 59,
      59, 59, 59, 59, 59, 59, 59, 59 }
  }
};

static const uint8_t kDefaultQuantMatrix[2][64] = {
  { 16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99 },
  { 17,  18,  24,  47,  99,  99,  99,  99,
    18,  21,  26,  66,  99,  99,  99,  99,
    24,  26,  56,  99,  99,  99,  99,  99,
    47,  66,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99 }
};

static const int kMaxQFactor = 64;

void FillQuantMatrix(bool is_chroma, uint32_t q, uint8_t dst[64]);
uint32_t FindBestMatrix(const int* src, bool is_chroma, uint8_t dst[64]);

}  // namespace pik

#endif  // JPEG_QUANT_TABLES_H_
