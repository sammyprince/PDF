//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfjbig2decoder.h"
#include "pdfexception.h"
#include "pdfccittfaxdecoder.h"

namespace pdf
{

/// Info structure for text region decoding structure
struct PDFJBIG2TextRegionDecodingParameters
{
    bool SBHUFF = false;
    bool SBREFINE = false;
    uint8_t SBDEFPIXEL = 0;
    PDFJBIG2BitOperation SBCOMBOP = PDFJBIG2BitOperation::Invalid;
    bool TRANSPOSED = false;
    uint8_t REFCORNER = 0;
    uint8_t SBDSOFFSET = 0;
    uint32_t SBW = 0;
    uint32_t SBH = 0;
    uint32_t SBNUMINSTANCES = 0;
    uint8_t SBSTRIPS = 0;
    uint32_t SBNUMSYMS = 0;
    std::vector<const PDFJBIG2Bitmap*> SBSYMS;
    uint8_t SBSYMCODELEN = 0;
    PDFJBIG2HuffmanDecoder SBSYMCODES;
    PDFJBIG2HuffmanDecoder SBHUFFFS;
    PDFJBIG2HuffmanDecoder SBHUFFDS;
    PDFJBIG2HuffmanDecoder SBHUFFDT;
    PDFJBIG2HuffmanDecoder SBHUFFRDW;
    PDFJBIG2HuffmanDecoder SBHUFFRDH;
    PDFJBIG2HuffmanDecoder SBHUFFRDX;
    PDFJBIG2HuffmanDecoder SBHUFFRDY;
    PDFJBIG2HuffmanDecoder SBHUFFRSIZE;
    uint8_t SBRTEMPLATE = 0;
    PDFJBIG2ATPositions SBRAT = { };
    PDFJBIG2ArithmeticDecoder* arithmeticDecoder = nullptr;
};

/// Info structure for bitmap decoding parameters
struct PDFJBIG2BitmapDecodingParameters
{
    /// Is Modified-Modified-Read encoding used? This encoding is simalr to CCITT pure 2D encoding.
    bool MMR = false;

    /// Is typical prediction for generic direct coding used?
    bool TPGDON = false;

    /// Width of the image
    int GBW = 0;

    /// Height of the image
    int GBH = 0;

    /// Template mode (not used for MMR).
    uint8_t GBTEMPLATE = 0;

    /// Positions of adaptative pixels
    PDFJBIG2ATPositions ATXY = { };

    /// Data with encoded image
    QByteArray data;

    /// State of arithmetic decoder
    PDFJBIG2ArithmeticDecoderState* arithmeticDecoderState = nullptr;

    /// Skip bitmap (pixel is skipped if corresponding pixel in the
    /// skip bitmap is 1). Set to nullptr, if not used.
    const PDFJBIG2Bitmap* SKIP = nullptr;

    /// Arithmetic decoder (used, if MMR == false)
    PDFJBIG2ArithmeticDecoder* arithmeticDecoder = nullptr;
};

/// Info structure for refinement bitmap decoding parameters
struct PDFJBIG2BitmapRefinementDecodingParameters
{
    /// Template mode used (0/1)
    uint8_t GRTEMPLATE = 0;

    /// Prediction (same as previous row)
    bool TPGRON = false;

    /// Bitmap width
    uint32_t GRW = 0;

    /// Bitmap height
    uint32_t GRH = 0;

    /// Reference bitmap
    const PDFJBIG2Bitmap* GRREFERENCE = nullptr;

    /// Offset x
    int32_t GRREFERENCEX = 0;

    /// Offset y
    int32_t GRREFERENCEY = 0;

    /// State of arithmetic decoder
    PDFJBIG2ArithmeticDecoderState* arithmeticDecoderState = nullptr;

    /// Positions of adaptative pixels
    PDFJBIG2ATPositions GRAT = { };
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_A[] =
{
    {     0, 1,  4,   0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    16, 2,  8,  0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   272, 3, 16, 0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 65808, 3, 32, 0b111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_B[] =
{
    {  0, 1,  0,      0b0,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1, 2,  0,     0b10,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 3,  0,    0b110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 4,  3,   0b1110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 11, 5,  6,  0b11110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  0, 6,  0, 0b111111, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    { 75, 6, 32, 0b111110,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_C[] =
{
    {    0, 1,  0,        0b0,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    1, 2,  0,       0b10,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 3,  0,      0b110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 4,  3,     0b1110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   11, 5,  6,    0b11110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 6,  0,   0b111110, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {   75, 7, 32,  0b1111110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -257, 8, 32, 0b11111111,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -256, 8,  8, 0b11111110,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_D[] =
{
    {  1, 1,  0,     0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 2,  0,    0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 3,  0,   0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  4, 4,  3,  0b1110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 12, 5,  6, 0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 76, 5, 32, 0b11111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_E[] =
{
    {    1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 2,  0,      0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 3,  0,     0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    4, 4,  3,    0b1110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   12, 5,  6,   0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   76, 6, 32,  0b111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -256, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -255, 7,  8, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_F[] =
{
    {     0, 2,  7,     0b00, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   128, 3,  7,    0b010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   256, 3,  8,    0b011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1024, 4,  9,   0b1000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -512, 4,  8,   0b1001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -256, 4,  7,   0b1010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -32, 4,  5,   0b1011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   512, 4,  9,   0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1024, 4, 10,   0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2048, 5, 10,  0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -128, 5,  6,  0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -64, 5,  5,  0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2049, 6, 32, 0b111110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  2048, 6, 32, 0b111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_G[] =
{
    {  -512, 3,  8,   0b000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   256, 3,  8,   0b001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   512, 3,  9,   0b010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1024, 3, 10,   0b011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1024, 4,  9,  0b1000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -256, 4,  7,  0b1001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -32, 4,  5,  0b1010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {     0, 4,  5,  0b1011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   128, 4,  7,  0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1025, 5, 32, 0b11110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  -128, 5,  6, 0b11010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -64, 5,  5, 0b11011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    32, 5,  5, 0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    64, 5,  6, 0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2048, 5, 32, 0b11111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_H[] =
{
    {    0, 2,  1,        0b00,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 2,  0,        0b01, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {    4, 3,  4,       0b100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -1, 4,  0,      0b1010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   22, 4,  4,      0b1011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   38, 4,  5,      0b1100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 5,  0,     0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   70, 5,  6,     0b11011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  134, 5,  7,     0b11100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 6,  0,    0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   20, 6,  1,    0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  262, 6,  7,    0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  646, 6, 10,    0b111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -2, 7,  0,   0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  390, 7,  8,   0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -15, 8,  3,  0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -5, 8,  1,  0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -16, 9, 32, 0b111111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {   -7, 9,  1, 0b111111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 9,  0, 0b111111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1670, 9, 32, 0b111111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_I[] =
{
    {    0, 2,  0,        0b00, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {   -1, 3,  1,       0b010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    1, 3,  1,       0b011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    7, 3,  5,       0b100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 4,  1,      0b1010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   43, 4,  5,      0b1011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   75, 4,  6,      0b1100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 5,  1,     0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  139, 5,  7,     0b11011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  267, 5,  8,     0b11100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    5, 6,  1,    0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   39, 6,  2,    0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  523, 6,  8,    0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1291, 6, 11,    0b111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -5, 7,  1,   0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  779, 7,  9,   0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -31, 8,  4,  0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -11, 8,  2,  0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -32, 9, 32, 0b111111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  -15, 9,  2, 0b111111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -7, 9,  1, 0b111111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 3339, 9, 32, 0b111111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_J[] =
{
    {   -2, 2,  2,       0b00,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 2,  0,       0b10, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {    6, 2,  6,       0b01,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 5,  0,    0b11000,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 5,  0,    0b11001,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   70, 5,  5,    0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 6,  0,   0b110110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  102, 6,  5,   0b110111,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  134, 6,  6,   0b111000,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  198, 6,  7,   0b111001,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  326, 6,  8,   0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  582, 6,  9,   0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1094, 6, 10,   0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -21, 7,  4,  0b1111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -4, 7,  0,  0b1111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    4, 7,  0,  0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 2118, 7, 11,  0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -22, 8, 32, 0b11111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {   -5, 8,  0, 0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    5, 8,  0, 0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 4166, 8, 32, 0b11111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_K[] =
{
    {   1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 2,  1,      0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   4, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 4,  1,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   7, 5,  1,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   9, 5,  2,   0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  13, 6,  2,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  17, 7,  2, 0b1111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  21, 7,  3, 0b1111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  29, 7,  4, 0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  45, 7,  5, 0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  77, 7,  6, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 141, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_L[] =
{
    {  1, 1,  0,        0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 2,  0,       0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 3,  1,      0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  5, 5,  0,    0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  6, 5,  1,    0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  8, 6,  1,   0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 10, 7,  0,  0b1111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 11, 7,  1,  0b1111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 13, 7,  2,  0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 17, 7,  3,  0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 25, 7,  4,  0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 41, 8,  5, 0b11111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 73, 8, 32, 0b11111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_M[] =
{
    {   1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 3,  0,     0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   7, 3,  3,     0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   3, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 4,  1,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   4, 5,  0,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  15, 6,  1,  0b111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  17, 6,  2,  0b111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  21, 6,  3,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  29, 6,  4,  0b111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  45, 6,  5,  0b111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  77, 7,  6, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 141, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_N[] =
{
    {  0, 1, 0,   0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2, 3, 0, 0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1, 3, 0, 0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1, 3, 0, 0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 3, 0, 0b111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_O[] =
{
    {   0, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -1, 3,  0,     0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   1, 3,  0,     0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -2, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 4,  0,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -4, 5,  1,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   3, 5,  1,   0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -8, 6,  2,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 6,  2,  0b111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -25, 7, 32, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -24, 7,  4, 0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   9, 7,  4, 0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  25, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

struct PDFJBIG2ArithmeticDecoderQeValue
{
    uint32_t Qe;        ///< Value of Qe
    uint8_t newMPS;     ///< New row if MPS (more probable symbol)
    uint8_t newLPS;     ///< New row if LPS (less probable symbol)
    uint8_t switchFlag; ///< Meaning of MPS/LPS is switched
};

static constexpr PDFJBIG2ArithmeticDecoderQeValue JBIG2_ARITHMETIC_DECODER_QE_VALUES[] =
{
    { 0x56010000, 1,   1, 1 },
    { 0x34010000, 2,   6, 0 },
    { 0x18010000, 3,   9, 0 },
    { 0x0AC10000, 4,  12, 0 },
    { 0x05210000, 5,  29, 0 },
    { 0x02210000, 38, 33, 0 },
    { 0x56010000, 7,   6, 1 },
    { 0x54010000, 8,  14, 0 },
    { 0x48010000, 9,  14, 0 },
    { 0x38010000, 10, 14, 0 },
    { 0x30010000, 11, 17, 0 },
    { 0x24010000, 12, 18, 0 },
    { 0x1C010000, 13, 20, 0 },
    { 0x16010000, 29, 21, 0 },
    { 0x56010000, 15, 14, 1 },
    { 0x54010000, 16, 14, 0 },
    { 0x51010000, 17, 15, 0 },
    { 0x48010000, 18, 16, 0 },
    { 0x38010000, 19, 17, 0 },
    { 0x34010000, 20, 18, 0 },
    { 0x30010000, 21, 19, 0 },
    { 0x28010000, 22, 19, 0 },
    { 0x24010000, 23, 20, 0 },
    { 0x22010000, 24, 21, 0 },
    { 0x1C010000, 25, 22, 0 },
    { 0x18010000, 26, 23, 0 },
    { 0x16010000, 27, 24, 0 },
    { 0x14010000, 28, 25, 0 },
    { 0x12010000, 29, 26, 0 },
    { 0x11010000, 30, 27, 0 },
    { 0x0AC10000, 31, 28, 0 },
    { 0x09C10000, 32, 29, 0 },
    { 0x08A10000, 33, 30, 0 },
    { 0x05210000, 34, 31, 0 },
    { 0x04410000, 35, 32, 0 },
    { 0x02A10000, 36, 33, 0 },
    { 0x02210000, 37, 34, 0 },
    { 0x01410000, 38, 35, 0 },
    { 0x01110000, 39, 36, 0 },
    { 0x00850000, 40, 37, 0 },
    { 0x00490000, 41, 38, 0 },
    { 0x00250000, 42, 39, 0 },
    { 0x00150000, 43, 40, 0 },
    { 0x00090000, 44, 41, 0 },
    { 0x00050000, 45, 42, 0 },
    { 0x00010000, 45, 43, 0 },
    { 0x56010000, 46, 46, 0 }
};

uint32_t PDFJBIG2ArithmeticDecoder::readByte(size_t context, PDFJBIG2ArithmeticDecoderState* state)
{
    uint32_t byte = 0;
    for (int i = 0; i < 8; ++i)
    {
        byte = (byte << 1) | readBit(context, state);
    }

    return byte;
}

int32_t PDFJBIG2ArithmeticDecoder::getIAID(uint32_t size, PDFJBIG2ArithmeticDecoderState* state)
{
    // Algorithm A.3 in annex A in the specification
    uint32_t PREV = 1;

    for (uint32_t i = 0; i < size; ++i)
    {
        uint32_t bit = readBit(PREV, state);
        PREV = (PREV << 1) | bit;
    }

    // Jakub Melka: we must subtract 1 << size, because at the start of the algorithm,
    // PREV is initialized to 1, which we don't want in the result, so we subtract the value.
    return int32_t(PREV) - int32_t(1 << size);
}

std::optional<int32_t> PDFJBIG2ArithmeticDecoder::getSignedInteger(PDFJBIG2ArithmeticDecoderState* state)
{
    // Algorithm A.2 in annex A in the specification
    uint32_t PREV = 1;

    auto readIntBit = [this, &PREV, state]()
    {
        uint32_t bit = readBit(PREV, state);

        if (PREV < 256)
        {
            PREV = (PREV << 1) | bit;
        }
        else
        {
            PREV = (((PREV << 1) | bit) & 0x01FF) | 0x0100;
        }
        Q_ASSERT(PREV < 512);

        return bit;
    };

    auto readIntBits = [&readIntBit](uint32_t bits)
    {
        uint32_t result = 0;

        for (uint32_t i = 0; i < bits; ++i)
        {
            result = (result << 1) | readIntBit();
        }

        return result;
    };

    uint32_t S = readIntBit(); // S = sign of number
    uint32_t V = 0; // V = value of number
    if (!readIntBit())
    {
        V = readIntBits(2);
    }
    else if (!readIntBit())
    {
        V = readIntBits(4) + 4;
    }
    else if (!readIntBit())
    {
        V = readIntBits(6) + 20;
    }
    else if (!readIntBit())
    {
        V = readIntBits(8) + 84;
    }
    else if (!readIntBit())
    {
        V = readIntBits(12) + 340;
    }
    else
    {
        V = readIntBits(32) + 4436;
    }

    if (S)
    {
        if (V == 0)
        {
            return std::nullopt;
        }
        else
        {
            return -static_cast<int32_t>(V);
        }
    }
    else
    {
        return V;
    }
}

void PDFJBIG2ArithmeticDecoder::perform_INITDEC()
{
    // Used figure G.1, in annex G, of specification
    uint32_t B = m_reader->readUnsignedByte();
    m_lastByte = B;
    m_c = B << 16;
    perform_BYTEIN();
    m_c = m_c << 7;
    m_ct -= 7;
    m_a = 0x80000000;
}

void PDFJBIG2ArithmeticDecoder::perform_BYTEIN()
{
    // Used figure G.3, in annex G, of specification
    if (m_lastByte == 0xFF)
    {
        const uint32_t B1 = m_reader->look(8);
        if (B1 > 0x8F)
        {
            m_c += 0xFF00;
            m_ct = 8;
        }
        else
        {
            m_c = m_c + (B1 << 9);
            m_ct = 7;
            m_lastByte = m_reader->readUnsignedByte();
        }
    }
    else
    {
        const uint32_t B = m_reader->readUnsignedByte();
        m_lastByte = B;
        m_c = m_c + (B << 8);
        m_ct = 8;
    }
}

uint32_t PDFJBIG2ArithmeticDecoder::perform_DECODE(size_t context, PDFJBIG2ArithmeticDecoderState* state)
{
    // Used figure G.2, in annex G, of specification
    const uint8_t QeRowIndex = state->getQeRowIndex(context);
    uint8_t MPS = state->getMPS(context);
    uint8_t D = MPS;

    // Sanity checks
    Q_ASSERT(QeRowIndex < std::size(JBIG2_ARITHMETIC_DECODER_QE_VALUES));
    Q_ASSERT(MPS < 2);

    const PDFJBIG2ArithmeticDecoderQeValue& QeInfo = JBIG2_ARITHMETIC_DECODER_QE_VALUES[QeRowIndex];
    const uint32_t Qe = QeInfo.Qe;
    m_a -= Qe;

    if (m_c >= Qe)
    {
        // We are substracting this value according figure E.15 in the specification
        m_c -= Qe;

        if ((m_a & 0x80000000) == 0)
        {
            // We must perform MPS_EXCHANGE algorithm, according to figure E.16, in annex E, of specification
            if (m_a < Qe)
            {
                D = 1 - MPS;
                if (QeInfo.switchFlag)
                {
                    MPS = 1 - MPS;
                }

                state->setQeRowIndexAndMPS(context, QeInfo.newLPS, MPS);
            }
            else
            {
                state->setQeRowIndexAndMPS(context, QeInfo.newMPS, MPS);
            }
        }
        else
        {
            // Do nothing, we are finished
            return D;
        }
    }
    else
    {
        // We must perform LPS_EXCHANGE algorithm, according to figure E.17, in annex E, of specification
        if (m_a < Qe)
        {
            state->setQeRowIndexAndMPS(context, QeInfo.newMPS, MPS);
        }
        else
        {
            D = 1 - MPS;
            if (QeInfo.switchFlag)
            {
                MPS = 1 - MPS;
            }
            state->setQeRowIndexAndMPS(context, QeInfo.newLPS, MPS);
        }

        m_a = Qe;
    }

    // Perform RENORMD algorithm, according to figure E.18, in annex E, of specification
    do
    {
        if (m_ct == 0)
        {
            perform_BYTEIN();
        }

        m_a = m_a << 1;
        m_c = m_c << 1;
        --m_ct;
    }
    while ((m_a & 0x80000000) == 0);

    return D;
}

PDFJBIG2SegmentHeader PDFJBIG2SegmentHeader::read(PDFBitReader* reader)
{
    PDFJBIG2SegmentHeader header;

    // Parse segment headers and segment flags
    header.m_segmentNumber = reader->readUnsignedInt();
    const uint8_t flags = reader->readUnsignedByte();
    const uint8_t type = flags & 0x3F;
    const bool isPageAssociationSize4ByteLong = flags & 0x40;

    // Jakub Melka: Now parse referred to segments. We do not use retain flags, so we skip
    // these bits. Data format is described in chapter 7.2.4 of the specification. According
    // the specification, values 5 or 6 can't be in bits 6,7,8, of the first byte. If these
    // occurs, exception is thrown.
    uint32_t retentionField = reader->readUnsignedByte();
    uint32_t referredSegmentsCount = retentionField >> 5; // Bits 6,7,8

    if (referredSegmentsCount == 5 || referredSegmentsCount == 6)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid header - bad referred segments."));
    }

    if (referredSegmentsCount == 7)
    {
        // This signalizes, that we have more than 4 referred segments. We will read 32-bit value,
        // where bits 0-28 will be number of referred segments, and bits 29-31 should be all set to 1.
        retentionField = (retentionField << 24) | reader->read(24);
        referredSegmentsCount = retentionField & 0x1FFFFFFF;

        if ((retentionField & 0xE0000000) != 0xE0000000)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid header - bad referred segments."));
        }

        // According the specification, retention header is 4 + ceil( (R + 1) / 8) bytes long. We have already 4 bytes read,
        // so only ceil( (R + 1) / 8 ) bytes we must skip. So, we will add 7 "bits", so we have (R + 1 + 7) / 8 bytes
        // to be skipped. We have R + 1 bits, not R bits, because 1 bit is used for this segment retain flag.
        const uint32_t bytesToSkip = (referredSegmentsCount + 8) / 8;
        reader->skipBytes(bytesToSkip);
    }

    // Read referred segment numbers. According to specification, chapter 7.2.5, referred segments should have
    // segment number lesser than actual segment number. So, if segment number is less, or equal to 256, then
    // 8-bit value is used to store referred segment number, if segment number is less, or equal to 65536, then
    // 16-bit value is used, otherwise 32 bit value is used.
    header.m_referredSegments.reserve(referredSegmentsCount);
    const PDFBitReader::Value referredSegmentNumberBits = (header.m_segmentNumber <= 256) ? 8 : ((header.m_segmentNumber <= 65536) ? 16 : 32);
    for (uint32_t i = 0; i < referredSegmentsCount; ++i)
    {
        header.m_referredSegments.push_back(reader->read(referredSegmentNumberBits));
    }

    header.m_pageAssociation = reader->read(isPageAssociationSize4ByteLong ? 32 : 8);
    header.m_segmentDataLength = reader->readUnsignedInt();
    header.m_lossless = type & 0x01;
    header.m_immediate = type & 0x02;

    switch (type)
    {
        case 0:
            header.m_segmentType = JBIG2SegmentType::SymbolDictionary;
            break;

        case 4:
        case 6:
        case 7:
            header.m_segmentType = JBIG2SegmentType::TextRegion;
            break;

        case 16:
            header.m_segmentType = JBIG2SegmentType::PatternDictionary;
            break;

        case 20:
        case 22:
        case 23:
            header.m_segmentType = JBIG2SegmentType::HalftoneRegion;
            break;

        case 36:
        case 38:
        case 39:
            header.m_segmentType = JBIG2SegmentType::GenericRegion;
            break;

        case 40:
        case 42:
        case 43:
            header.m_segmentType = JBIG2SegmentType::GenericRefinementRegion;
            break;

        case 48:
            header.m_segmentType = JBIG2SegmentType::PageInformation;
            break;

        case 49:
            header.m_segmentType = JBIG2SegmentType::EndOfPage;
            break;

        case 50:
            header.m_segmentType = JBIG2SegmentType::EndOfStripe;
            break;

        case 51:
            header.m_segmentType = JBIG2SegmentType::EndOfFile;
            break;

        case 52:
            header.m_segmentType = JBIG2SegmentType::Profiles;
            break;

        case 53:
            header.m_segmentType = JBIG2SegmentType::Tables;
            break;

        case 62:
            header.m_segmentType = JBIG2SegmentType::Extension;
            break;

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid segment type %1.").arg(type));
    }

    return header;
}

PDFJBIG2Decoder::~PDFJBIG2Decoder()
{

}

PDFImageData PDFJBIG2Decoder::decode(PDFImageData::MaskingType maskingType)
{
    for (const QByteArray* data :  { &m_globalData, &m_data })
    {
        if (!data->isEmpty())
        {
            m_reader = PDFBitReader(data, 8);
            processStream();
        }
    }

    if (m_pageBitmap.isValid())
    {
        PDFBitWriter writer(1);

        const int columns = m_pageBitmap.getWidth();
        const int rows = m_pageBitmap.getHeight();

        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                writer.write(m_pageBitmap.getPixel(column, row));
            }
            writer.finishLine();
        }

        return PDFImageData(1, 1, static_cast<uint32_t>(columns), static_cast<uint32_t>(rows), static_cast<uint32_t>((columns + 7) / 8), maskingType, writer.takeByteArray(), { }, { }, { });
    }

    return PDFImageData();
}

PDFImageData PDFJBIG2Decoder::decodeFileStream()
{
    m_reader = PDFBitReader(&m_data, 8);

    constexpr const char* JBIG2_FILE_HEADER = "\x97\x4A\x42\x32\x0D\x0A\x1A\x0A";
    if (!m_data.startsWith(JBIG2_FILE_HEADER))
    {
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file header."));
    }

    m_reader.skipBytes(std::strlen(JBIG2_FILE_HEADER));

    // File flags
    const uint8_t fileFlags = m_reader.readUnsignedByte();

    if (fileFlags & 0xFC)
    {
        // Jakub Melka: According the specification, bits 2-7 should be reserved and zero.
        // If they are nonzero, probably a new version of JBIG2 format exists, but
        // is not decodable by this decoder. So, in this case, we don't do decoding
        // and report error immediately.
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file header flags."));
    }

    const bool isFileOrganizationSequential = fileFlags & 0x01;
    const bool isUknownNumberOfPages = fileFlags & 0x02;

    if (isUknownNumberOfPages)
    {
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file - unknown number of pages."));
    }

    const uint32_t numberOfPages = m_reader.readUnsignedInt();
    if (numberOfPages != 1)
    {
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file - invalid number of pages (%1).").arg(numberOfPages));
    }

    if (isFileOrganizationSequential)
    {
        // We are lucky, file organization is sequential. Just copy the data.
        m_data = m_reader.readSubstream(-1);
    }
    else
    {
        // We must transform random organization to the sequential one
        QByteArray sequentialData;

        struct SegmentInfo
        {
            PDFJBIG2SegmentHeader header;
            QByteArray headerData;
            QByteArray segmentData;
        };

        std::vector<SegmentInfo> segmentInfos;
        while (true)
        {
            SegmentInfo segmentInfo;
            const int headerStartPosition = m_reader.getPosition();
            segmentInfo.header = PDFJBIG2SegmentHeader::read(&m_reader);
            const int headerEndPosition = m_reader.getPosition();
            segmentInfo.headerData = m_data.mid(headerStartPosition, headerEndPosition - headerStartPosition);
            segmentInfos.push_back(qMove(segmentInfo));

            if (segmentInfo.header.getSegmentType() == JBIG2SegmentType::EndOfFile)
            {
                break;
            }
        }

        for (SegmentInfo& info : segmentInfos)
        {
            if (!info.header.isSegmentDataLengthDefined())
            {
                throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file - segment length is not defined."));
            }

            info.segmentData = m_reader.readSubstream(info.header.getSegmentDataLength());
        }

        for (const SegmentInfo& info : segmentInfos)
        {
            if (info.header.getSegmentType() == JBIG2SegmentType::EndOfPage ||
                info.header.getSegmentType() == JBIG2SegmentType::EndOfFile)
            {
                continue;
            }

            sequentialData.append(info.headerData);
            sequentialData.append(info.segmentData);
        }

        m_data = qMove(sequentialData);
    }

    return decode(PDFImageData::MaskingType::None);
}

void PDFJBIG2Decoder::processStream()
{
    while (!m_reader.isAtEnd())
    {
        // Read the segment header, then process the segment data
        PDFJBIG2SegmentHeader segmentHeader = PDFJBIG2SegmentHeader::read(&m_reader);
        const int64_t segmentDataStartPosition = m_reader.getPosition();

        switch (segmentHeader.getSegmentType())
        {
            case JBIG2SegmentType::SymbolDictionary:
                processSymbolDictionary(segmentHeader);
                break;

            case JBIG2SegmentType::TextRegion:
                processTextRegion(segmentHeader);
                break;

            case JBIG2SegmentType::PatternDictionary:
                processPatternDictionary(segmentHeader);
                break;

            case JBIG2SegmentType::HalftoneRegion:
                processHalftoneRegion(segmentHeader);
                break;

            case JBIG2SegmentType::GenericRegion:
                processGenericRegion(segmentHeader);
                break;

            case JBIG2SegmentType::GenericRefinementRegion:
                processGenericRefinementRegion(segmentHeader);
                break;

            case JBIG2SegmentType::PageInformation:
                processPageInformation(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfPage:
                processEndOfPage(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfStripe:
                processEndOfStripe(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfFile:
                processEndOfFile(segmentHeader);
                break;

            case JBIG2SegmentType::Profiles:
                processProfiles(segmentHeader);
                break;

            case JBIG2SegmentType::Tables:
                processCodeTables(segmentHeader);
                break;

            case JBIG2SegmentType::Extension:
                processExtension(segmentHeader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid segment type %1.").arg(static_cast<uint32_t>(segmentHeader.getSegmentType())));
        }

        // Make sure, that all data are processed by segment header. Positive offset means,
        // that we did not read all the data bytes. Negative offset means, that we read more
        // bytes in segment handler, that the segment has specified.
        if (segmentHeader.isSegmentDataLengthDefined())
        {
            const int64_t offset = static_cast<int64_t>(segmentDataStartPosition) + static_cast<int64_t>(segmentHeader.getSegmentDataLength()) - static_cast<int64_t>(m_reader.getPosition());
            if (offset > 0)
            {
                m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 bad segment data - handler doesn't process all segment data - %1 bytes left.").arg(offset));
            }
            else if (offset < 0)
            {
                // This is fatal error, we have read data, which doesn't belong to this segment
                throw PDFException(PDFTranslationContext::tr("JBIG2 bad segment data - handler reads %1 bytes past segment end.").arg(-offset));
            }

            // Always seek to the right position
            m_reader.seek(segmentDataStartPosition + segmentHeader.getSegmentDataLength());
        }
    }
}

void PDFJBIG2Decoder::processSymbolDictionary(const PDFJBIG2SegmentHeader& header)
{
    /* 7.4.2.2 step 1) */
    PDFJBIG2SymbolDictionaryDecodingParameters parameters;
    const uint16_t symbolDictionaryFlags = m_reader.readUnsignedWord();
    parameters.SDHUFF = symbolDictionaryFlags & 0x0001;
    parameters.SDREFAGG = symbolDictionaryFlags & 0x0002;
    parameters.SDHUFFDH = (symbolDictionaryFlags >> 2) & 0x0003;
    parameters.SDHUFFDH = (symbolDictionaryFlags >> 4) & 0x0003;
    parameters.SDHUFFBMSIZE = (symbolDictionaryFlags >> 6) & 0x0001;
    parameters.SDHUFFAGGINST = (symbolDictionaryFlags >> 7) & 0x0001;
    parameters.isArithmeticCodingStateUsed = (symbolDictionaryFlags >> 8) & 0x0001;
    parameters.isArithmeticCodingStateRetained = (symbolDictionaryFlags >> 9) & 0x0001;
    parameters.SDTEMPLATE = (symbolDictionaryFlags >> 10) & 0x0003;
    parameters.SDRTEMPLATE = (symbolDictionaryFlags >> 12) & 0x0001;
    parameters.SDAT = readATTemplatePixelPositions((parameters.SDHUFF == 0) ? ((parameters.SDTEMPLATE == 0) ? 4 : 1) : 0);
    parameters.SDRAT = readATTemplatePixelPositions((parameters.SDREFAGG == 1 && parameters.SDRTEMPLATE == 0) ? 2 : 0);
    parameters.SDNUMEXSYMS = m_reader.readUnsignedInt();
    parameters.SDNUMNEWSYMS = m_reader.readUnsignedInt();

    /* sanity checks */

    if ((symbolDictionaryFlags >> 13) != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
    }

    if (!parameters.SDHUFF || !parameters.SDREFAGG)
    {
        if (parameters.SDHUFFAGGINST != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }

    if (!parameters.SDHUFF)
    {
        if (parameters.SDHUFFDH != 0 || parameters.SDHUFFDH != 0 || parameters.SDHUFFBMSIZE != 0 || parameters.SDHUFFAGGINST != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }
    else
    {
        if (!parameters.SDREFAGG && (parameters.isArithmeticCodingStateUsed || parameters.isArithmeticCodingStateRetained || parameters.SDRTEMPLATE != 0))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }

        if (parameters.SDTEMPLATE != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }

    /* 7.4.2.2 step 2) */
    PDFJBIG2ReferencedSegments references = getReferencedSegments(header);
    parameters.SDINSYMS = references.getSymbolBitmaps();
    parameters.SDNUMINSYMS = static_cast<uint32_t>(parameters.SDINSYMS.size());

    /* 7.4.2.1.6 - huffman table selection */

    if (parameters.SDHUFF)
    {
        switch (parameters.SDHUFFDH)
        {
            case 0:
                parameters.SDHUFFDH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_D), std::end(PDFJBIG2StandardHuffmanTable_D));
                break;

            case 1:
                parameters.SDHUFFDH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_E), std::end(PDFJBIG2StandardHuffmanTable_E));
                break;

            case 3:
                parameters.SDHUFFDH_Decoder = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (parameters.SDHUFFDW)
        {
            case 0:
                parameters.SDHUFFDW_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_B), std::end(PDFJBIG2StandardHuffmanTable_B));
                break;

            case 1:
                parameters.SDHUFFDW_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_C), std::end(PDFJBIG2StandardHuffmanTable_C));
                break;

            case 3:
                parameters.SDHUFFDW_Decoder = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (parameters.SDHUFFBMSIZE)
        {
            case 0:
                parameters.SDHUFFBMSIZE_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
                break;

            case 1:
                parameters.SDHUFFBMSIZE_Decoder = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (parameters.SDHUFFAGGINST)
        {
            case 0:
                parameters.SDHUFFAGGINST_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
                break;

            case 1:
                parameters.SDHUFFAGGINST_Decoder = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        parameters.EXRUNLENGTH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));

        if (references.currentUserCodeTableIndex != references.codeTables.size())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid number of huffam code table - %1 unused.").arg(references.codeTables.size() - references.currentUserCodeTableIndex));
        }
    }
    else
    {
        /* 7.4.2.2 step 3) and 4) - initialize arithmetic encoder */
        if (parameters.isArithmeticCodingStateUsed)
        {
            if (references.symbolDictionaries.empty())
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 trying to use aritmetic decoder context from previous symbol dictionary, but it doesn't exist."));
            }

            resetArithmeticStatesGeneric(parameters.SDTEMPLATE, &references.symbolDictionaries.back()->getGenericState());
        }
        else
        {
            resetArithmeticStatesGeneric(parameters.SDTEMPLATE, nullptr);
        }

        if (parameters.SDREFAGG)
        {
            if (parameters.isArithmeticCodingStateUsed)
            {
                if (references.symbolDictionaries.empty())
                {
                    throw PDFException(PDFTranslationContext::tr("JBIG2 trying to use aritmetic decoder context from previous symbol dictionary, but it doesn't exist."));
                }

                resetArithmeticStatesGenericRefinement(parameters.SDRTEMPLATE, &references.symbolDictionaries.back()->getGenericRefinementState());
            }
            else
            {
                resetArithmeticStatesGenericRefinement(parameters.SDRTEMPLATE, nullptr);
            }
        }
    }

    PDFJBIG2ArithmeticDecoder decoder(&m_reader);
    PDFJBIG2ArithmeticDecoderState IADH;
    PDFJBIG2ArithmeticDecoderState IADW;
    PDFJBIG2ArithmeticDecoderState IAEX;
    if (!parameters.SDHUFF)
    {
        decoder.initialize();
        IADH.reset(9);
        IADW.reset(9);
        IAEX.reset(9);
    }

    /* 6.5.5 - algorithm for decoding symbol dictionary */

    /* 6.5.5 step 1) - create output bitmaps */
    parameters.SDNEWSYMS.resize(parameters.SDNUMNEWSYMS);

    /* 6.5.5 step 2) - initalize width array */
    if (parameters.SDHUFF == 1 && parameters.SDREFAGG == 0)
    {
        parameters.SDNEWSYMWIDTHS.resize(parameters.SDNUMNEWSYMS, 0);
    }

    /* 6.5.5 step 3) - initalize variables to zero */
    uint32_t HCHEIGHT = 0;
    uint32_t NSYMSDECODED = 0;

    /* 6.5.5 step 4) - read all bitmaps */
    while (NSYMSDECODED < parameters.SDNUMNEWSYMS)
    {
        /* 6.5.5 step 4) b) - decode height class delta height according to 6.5.6 */
        int32_t HCDH = checkInteger(parameters.SDHUFF ? parameters.SDHUFFDH_Decoder.readSignedInteger() : decoder.getSignedInteger(&IADH));
        HCHEIGHT += HCDH;
        uint32_t SYMWIDTH = 0;
        uint32_t TOTWIDTH = 0;
        uint32_t HCFIRSTSYM = NSYMSDECODED;

        /* 6.5.5 step 4) c) - read height class */
        while (NSYMSDECODED < parameters.SDNUMNEWSYMS)
        {
            /* 6.5.5 step 4) c) i) - Delta width acc. to 6.5.7 */
            std::optional<int32_t> DW = parameters.SDHUFF ? parameters.SDHUFFDW_Decoder.readSignedInteger() : decoder.getSignedInteger(&IADW);

            if (!DW.has_value())
            {
                // All symbols of this height class have been decoded
                break;
            }

            if (NSYMSDECODED >= parameters.SDNUMNEWSYMS)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 symbol height class has more symbols, than defined in the symbol dictionary header."));
            }

            SYMWIDTH += *DW;
            TOTWIDTH += SYMWIDTH;

            if (parameters.SDHUFF == 0 || parameters.SDREFAGG == 1)
            {
                /* 6.5.5 step 4) c) ii) - read bitmap acc. to 6.5.8 */

                if (parameters.SDREFAGG == 0)
                {
                    /* 6.5.8.1 Direct-coded symbol bitmap, using Table 16 */
                    PDFJBIG2BitmapDecodingParameters bitmapParameters;
                    bitmapParameters.MMR = false;
                    bitmapParameters.GBW = SYMWIDTH;
                    bitmapParameters.GBH = HCHEIGHT;
                    bitmapParameters.GBTEMPLATE = parameters.SDTEMPLATE;
                    bitmapParameters.TPGDON = false;
                    bitmapParameters.ATXY = parameters.SDAT;
                    bitmapParameters.arithmeticDecoder = &decoder;
                    bitmapParameters.arithmeticDecoderState = &m_arithmeticDecoderStates[Generic];
                    parameters.SDNEWSYMS[NSYMSDECODED] = readBitmap(bitmapParameters);
                }
                else
                {
                    /* 6.5.8.2 Refinement/aggregate-coded symbol bitmap */
                    // TODO: JBIG2 read bitmap
                }
            }
            else
            {
                /* 6.5.5 step 4) c) iii) - update value of widths */
                parameters.SDNEWSYMWIDTHS[NSYMSDECODED] = SYMWIDTH;
            }

            /* 6.5.5 step 4) c) iv) - update decoded symbols counter */
            ++NSYMSDECODED;
        }

        /* 6.5.5 step 4) d) - create collective bitmap (if it does exist) */
        // TODO: JBIG2 - create collective bitmap
    }

    /* 6.5.5 step 5) - determine exports according to 6.5.10 */
    std::vector<bool> EXFLAGS;
    const size_t symbolsSize = parameters.SDNUMINSYMS + parameters.SDNEWSYMS.size();
    EXFLAGS.reserve(symbolsSize);
    bool CUREXFLAG = false;
    while (EXFLAGS.size() < symbolsSize)
    {
        const uint32_t EXRUNLENGTH = static_cast<uint32_t>(checkInteger(parameters.SDHUFF ? parameters.EXRUNLENGTH_Decoder.readSignedInteger() : decoder.getSignedInteger(&IAEX)));
        EXFLAGS.insert(EXFLAGS.end(), EXRUNLENGTH, CUREXFLAG);
        CUREXFLAG = !CUREXFLAG;
    }
    m_reader.alignToBytes();
    if (!parameters.SDHUFF)
    {
        // Skipneme 1 byte na konci
        m_reader.skipBytes(1);
    }

    std::vector<PDFJBIG2Bitmap> bitmaps;
    bitmaps.reserve(parameters.SDNUMEXSYMS);

    // Insert input bitmaps
    for (size_t i = 0; i < parameters.SDNUMINSYMS; ++i)
    {
        if (EXFLAGS[i])
        {
            bitmaps.push_back(*parameters.SDINSYMS[i]);
        }
    }

    // Insert output bitmaps
    for (size_t i = 0; i < NSYMSDECODED; ++i)
    {
        if (EXFLAGS[i + parameters.SDNUMINSYMS])
        {
            bitmaps.push_back(parameters.SDNEWSYMS[i]);
        }
    }

    PDFJBIG2ArithmeticDecoderState savedGeneric;
    PDFJBIG2ArithmeticDecoderState savedRefine;

    if (parameters.isArithmeticCodingStateRetained)
    {
        savedGeneric = qMove(m_arithmeticDecoderStates[Generic]);
        savedRefine = qMove(m_arithmeticDecoderStates[Refinement]);
    }

    m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2SymbolDictionary>(qMove(bitmaps), qMove(savedGeneric), qMove(savedRefine));
}

void PDFJBIG2Decoder::processTextRegion(const PDFJBIG2SegmentHeader& header)
{
    auto getSBCOMBOOP = [](const uint8_t value)
    {
        switch (value)
        {
            case 0:
                return PDFJBIG2BitOperation::Or;

            case 1:
                return PDFJBIG2BitOperation::And;

            case 2:
                return PDFJBIG2BitOperation::Xor;

            case 3:
                return PDFJBIG2BitOperation::NotXor;

            default:
                break;
        }

        Q_ASSERT(false);
        return PDFJBIG2BitOperation::Invalid;
    };

    PDFJBIG2RegionSegmentInformationField regionSegmentInfo = readRegionSegmentInformationField();
    const uint16_t flags = m_reader.readUnsignedWord();
    const bool SBHUFF = flags & 0x0001;
    const bool SBREFINE = flags & 0x0002;
    const uint8_t SBSTRIPS = 1 << ((flags >> 2) & 0x03);
    const uint8_t REFCORNER = (flags >> 4) & 0x03;
    const bool TRANSPOSED = (flags >> 6) & 0x01;
    const uint8_t SBCOMBOOP_value = (flags >> 7) & 0x03;
    const PDFJBIG2BitOperation SBCOMBOOP = getSBCOMBOOP(SBCOMBOOP_value);
    const uint8_t SBDEFPIXEL = ((flags >> 9) & 0x01) ? 0xFF : 0x00;
    const uint8_t SBDSOFFSET = (flags >> 10) & 0x1F;
    const uint8_t SBRTEMPLATE = (flags >> 15) & 0x01;

    // Decoding parameters
    PDFJBIG2TextRegionDecodingParameters parameters;
    parameters.SBHUFF = SBHUFF;
    parameters.SBREFINE = SBREFINE;
    parameters.SBDEFPIXEL = SBDEFPIXEL;
    parameters.SBCOMBOP = SBCOMBOOP;
    parameters.TRANSPOSED = TRANSPOSED;
    parameters.REFCORNER = REFCORNER;
    parameters.SBDSOFFSET = SBDSOFFSET;
    parameters.SBW = regionSegmentInfo.width;
    parameters.SBH = regionSegmentInfo.height;
    parameters.SBRTEMPLATE = SBRTEMPLATE;
    parameters.SBSTRIPS = SBSTRIPS;

    // Referenced segments data
    PDFJBIG2ReferencedSegments references = getReferencedSegments(header);

    if (SBHUFF)
    {
        uint16_t huffmanFlags = m_reader.readUnsignedWord();

        auto readHuffmanTableSelection = [&huffmanFlags]() -> const uint8_t
        {
            const uint8_t result = huffmanFlags & 0x03;
            huffmanFlags = huffmanFlags >> 2;
            return result;
        };

        const uint8_t SBHUFFFS = readHuffmanTableSelection();
        const uint8_t SBHUFFDS = readHuffmanTableSelection();
        const uint8_t SBHUFFDT = readHuffmanTableSelection();
        const uint8_t SBHUFFRDW = readHuffmanTableSelection();
        const uint8_t SBHUFFRDH = readHuffmanTableSelection();
        const uint8_t SBHUFFRDX = readHuffmanTableSelection();
        const uint8_t SBHUFFRDY = readHuffmanTableSelection();
        const uint8_t SBHUFFRSIZE = readHuffmanTableSelection();

        if (huffmanFlags)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid huffman table flags in text region segment."));
        }

        // Create huffman tables
        switch (SBHUFFFS)
        {
            case 0:
                parameters.SBHUFFFS = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_F), std::end(PDFJBIG2StandardHuffmanTable_F));
                break;

            case 1:
                parameters.SBHUFFFS = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_G), std::end(PDFJBIG2StandardHuffmanTable_G));
                break;

            case 3:
                parameters.SBHUFFFS = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFDS)
        {
            case 0:
                parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_H), std::end(PDFJBIG2StandardHuffmanTable_H));
                break;

            case 1:
                parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_I), std::end(PDFJBIG2StandardHuffmanTable_I));
                break;

            case 2:
                parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_J), std::end(PDFJBIG2StandardHuffmanTable_J));
                break;

            case 3:
                parameters.SBHUFFDS = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFDT)
        {
            case 0:
                parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_K), std::end(PDFJBIG2StandardHuffmanTable_K));
                break;

            case 1:
                parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_L), std::end(PDFJBIG2StandardHuffmanTable_L));
                break;

            case 2:
                parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_M), std::end(PDFJBIG2StandardHuffmanTable_M));
                break;

            case 3:
                parameters.SBHUFFDT = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDW)
        {
            case 0:
                parameters.SBHUFFRDW = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDW = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDW = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDH)
        {
            case 0:
                parameters.SBHUFFRDH = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDH = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDH = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDX)
        {
            case 0:
                parameters.SBHUFFRDX = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDX = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDX = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDY)
        {
            case 0:
                parameters.SBHUFFRDY = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDY = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDY = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRSIZE)
        {
            case 0:
                parameters.SBHUFFRSIZE = PDFJBIG2HuffmanDecoder(&m_reader, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
                break;

            case 1:
                parameters.SBHUFFRSIZE = references.getUserTable(&m_reader);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }
    }

    if (SBREFINE && SBRTEMPLATE == 0)
    {
        parameters.SBRAT = readATTemplatePixelPositions(2);
    }

    parameters.SBSYMS = references.getSymbolBitmaps();
    parameters.SBNUMSYMS = static_cast<uint32_t>(parameters.SBSYMS.size());
    parameters.SBNUMINSTANCES = m_reader.readUnsignedInt();
    parameters.SBSYMCODELEN = log2ceil(parameters.SBNUMSYMS);

    if (parameters.SBNUMSYMS == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 no referred symbols in text region segment."));
    }

    PDFJBIG2ArithmeticDecoder decoder(&m_reader);
    if (SBHUFF)
    {
        // Read run code lengths
        std::vector<PDFJBIG2HuffmanTableEntry> rangeLengthTable(35, PDFJBIG2HuffmanTableEntry());
        for (int32_t i = 0; i < rangeLengthTable.size(); ++i)
        {
            rangeLengthTable[i].value = i;
            rangeLengthTable[i].prefixBitLength = m_reader.read(4);
        }
        rangeLengthTable = PDFJBIG2HuffmanCodeTable::buildPrefixes(rangeLengthTable);
        PDFJBIG2HuffmanDecoder runLengthDecoder(&m_reader, qMove(rangeLengthTable));

        std::vector<PDFJBIG2HuffmanTableEntry> symCodeTable(parameters.SBNUMSYMS, PDFJBIG2HuffmanTableEntry());
        for (uint32_t i = 0; i < parameters.SBNUMSYMS;)
        {
            symCodeTable[i].value = i;
            uint32_t code = checkInteger(runLengthDecoder.readSignedInteger());
            switch (code)
            {
                default:
                    symCodeTable[i++].prefixBitLength = code;
                    break;

                case 32:
                case 33:
                case 34:
                {
                    uint32_t length = 0;
                    uint32_t range = 0;

                    if (code == 32)
                    {
                        if (i == 0)
                        {
                            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol length code table for text region segment."));
                        }
                        length = symCodeTable[i - 1].prefixBitLength;
                    }

                    switch (code)
                    {
                        case 32:
                            range = m_reader.read(2) + 3;
                            break;

                        case 33:
                            range = m_reader.read(3) + 3;
                            break;

                        case 34:
                            range = m_reader.read(7) + 11;
                            break;

                        default:
                            Q_ASSERT(false);
                            break;
                    }

                    for (uint32_t j = 0; j < range; ++j)
                    {
                        symCodeTable[i].value = i;
                        symCodeTable[i].prefixBitLength = length;
                        ++i;
                    }
                    break;
                }
            }
        }
        symCodeTable = PDFJBIG2HuffmanCodeTable::buildPrefixes(symCodeTable);
        parameters.SBSYMCODES = PDFJBIG2HuffmanDecoder(&m_reader, qMove(symCodeTable));
        m_reader.alignToBytes();
    }
    else
    {
        // Arithmetic decoder
        decoder.initialize();
        parameters.arithmeticDecoder = &decoder;
    }

    if (parameters.SBREFINE)
    {
        resetArithmeticStatesGenericRefinement(parameters.SBRTEMPLATE, nullptr);
    }

    PDFJBIG2Bitmap bitmap = readTextBitmap(parameters);
    if (bitmap.isValid())
    {
        if (header.isImmediate())
        {
            m_pageBitmap.paint(bitmap, regionSegmentInfo.offsetX, regionSegmentInfo.offsetY, regionSegmentInfo.operation, m_pageSizeUndefined, m_pageDefaultPixelValue);
        }
        else
        {
            m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(bitmap));
        }
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap for generic region."));
    }
}

void PDFJBIG2Decoder::processPatternDictionary(const PDFJBIG2SegmentHeader& header)
{
    // TODO: JBIG2 - processPatternDictionary
    throw PDFException(PDFTranslationContext::tr("JBIG2 NOT IMPLEMENTED."));
}

void PDFJBIG2Decoder::processHalftoneRegion(const PDFJBIG2SegmentHeader& header)
{
    // TODO: JBIG2 - processHalftoneRegion
    throw PDFException(PDFTranslationContext::tr("JBIG2 NOT IMPLEMENTED."));
}

void PDFJBIG2Decoder::processGenericRegion(const PDFJBIG2SegmentHeader& header)
{
    const int segmentStartPosition = m_reader.getPosition();
    PDFJBIG2RegionSegmentInformationField field = readRegionSegmentInformationField();
    const uint8_t flags = m_reader.readUnsignedByte();

    PDFJBIG2BitmapDecodingParameters parameters;
    parameters.MMR = flags & 0b0001;
    parameters.TPGDON = flags & 0b1000;
    parameters.GBTEMPLATE = (flags >> 1) & 0b0011;

    if ((flags & 0b11110000) != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - malformed generic region flags."));
    }

    if (!parameters.MMR)
    {
        // We will use arithmetic coding, read template pixels and reset arithmetic coder state
        parameters.ATXY = readATTemplatePixelPositions((parameters.GBTEMPLATE == 0) ? 4 : 1);
        resetArithmeticStatesGeneric(parameters.GBTEMPLATE, nullptr);
    }

    // Determine segment data length
    const int segmentDataStartPosition = m_reader.getPosition();
    const int segmentHeaderBytes = segmentDataStartPosition - segmentStartPosition;
    int segmentDataBytes = 0;
    if (header.isSegmentDataLengthDefined())
    {
        segmentDataBytes = header.getSegmentDataLength() - segmentHeaderBytes;
    }
    else
    {
        // We must find byte sequence { 0x00, 0x00 } for MMR and { 0xFF, 0xAC } for arithmetic decoder
        const QByteArray* stream = m_reader.getStream();

        QByteArray endSequence(2, 0);
        if (!parameters.MMR)
        {
            endSequence[0] = char(0xFF);
            endSequence[1] = char(0xAC);
        }

        int endPosition = stream->indexOf(endSequence);
        if (endPosition == -1)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 - end of data byte sequence not found for generic region."));
        }

        // Add end bytes (they are also a part of stream)
        endPosition += endSequence.size();

        segmentDataBytes = endPosition - segmentDataStartPosition;
    }

    parameters.data = m_reader.getStream()->mid(segmentDataStartPosition, segmentDataBytes);
    parameters.GBW = field.width;
    parameters.GBH = field.height;
    parameters.arithmeticDecoderState = &m_arithmeticDecoderStates[Generic];


    PDFBitReader reader(&parameters.data, 1);
    PDFJBIG2ArithmeticDecoder decoder(&reader);

    if (!parameters.MMR)
    {
        decoder.initialize();
        parameters.arithmeticDecoder = &decoder;
    }

    PDFJBIG2Bitmap bitmap = readBitmap(parameters);
    if (bitmap.isValid())
    {
        if (header.isImmediate())
        {
            m_pageBitmap.paint(bitmap, field.offsetX, field.offsetY, field.operation, m_pageSizeUndefined, m_pageDefaultPixelValue);
        }
        else
        {
            m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(bitmap));
        }
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap for generic region."));
    }

    // Now skip the data
    m_reader.skipBytes(segmentDataBytes);

    if (header.isImmediate() && !header.isSegmentDataLengthDefined())
    {
        m_reader.skipBytes(4);
    }
}

void PDFJBIG2Decoder::processGenericRefinementRegion(const PDFJBIG2SegmentHeader& header)
{
    PDFJBIG2RegionSegmentInformationField field = readRegionSegmentInformationField();
    const uint8_t flags = m_reader.readUnsignedByte();

    if ((flags & 0b11111100) != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid flags for generic refinement region."));
    }

    const uint8_t GRTEMPLATE = flags & 0x01;
    const bool TPGRON = flags & 0x02;

    PDFJBIG2ATPositions GRAT = { };
    if (GRTEMPLATE == 0)
    {
        GRAT = readATTemplatePixelPositions(2);
    }

    PDFJBIG2Bitmap GRREFERENCE;
    const std::vector<uint32_t>& referredSegments = header.getReferredSegments();
    switch (referredSegments.size())
    {
        case 0:
        {
            // According the specification, operator must be REPLACE
            if (field.operation != PDFJBIG2BitOperation::Replace)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 - operation must be REPLACE for generic refinement region."));
            }

            GRREFERENCE = m_pageBitmap.getSubbitmap(field.offsetX, field.offsetY, field.width, field.height);
            break;
        }

        case 1:
        {
            GRREFERENCE = getBitmap(referredSegments.front(), true);
            break;
        }

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid referred segments (%1) for generic refinement region.").arg(referredSegments.size()));
    }

    if (GRREFERENCE.getWidth() != field.width || GRREFERENCE.getHeight() != field.height)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid referred bitmap size [%1 x %2] instead of [%3 x %4] for generic refinement region.").arg(GRREFERENCE.getWidth()).arg(GRREFERENCE.getHeight()).arg(field.width).arg(field.height));
    }

    resetArithmeticStatesGenericRefinement(GRTEMPLATE, nullptr);

    PDFJBIG2BitmapRefinementDecodingParameters parameters;
    parameters.GRTEMPLATE = GRTEMPLATE;
    parameters.TPGRON = TPGRON;
    parameters.GRW = field.width;
    parameters.GRH = field.height;
    parameters.GRAT = GRAT;
    parameters.arithmeticDecoderState = &m_arithmeticDecoderStates[Refinement];
    parameters.GRREFERENCE = &GRREFERENCE;
    parameters.GRREFERENCEX = 0;
    parameters.GRREFERENCEY = 0;

    PDFJBIG2Bitmap refinementBitmap = readRefinementBitmap(parameters);
    if (refinementBitmap.isValid())
    {
        if (header.isImmediate())
        {
            m_pageBitmap.paint(refinementBitmap, field.offsetX, field.offsetY, field.operation, m_pageSizeUndefined, m_pageDefaultPixelValue);
        }
        else
        {
            m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(refinementBitmap));
        }
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap for generic refinement region."));
    }
}

void PDFJBIG2Decoder::processPageInformation(const PDFJBIG2SegmentHeader&)
{
    const uint32_t width = m_reader.readUnsignedInt();
    const uint32_t height = m_reader.readUnsignedInt();

    // Skip 8 bites - resolution. We do not need the resolution values.
    m_reader.skipBytes(sizeof(uint32_t) * 2);

    const uint8_t flags = m_reader.readUnsignedByte();
    const uint16_t striping = m_reader.readUnsignedWord();

    m_pageDefaultPixelValue = (flags & 0x04) ? 0xFF : 0x00;
    m_pageDefaultCompositionOperatorOverriden = (flags & 0x40);

    const uint8_t defaultOperator = (flags >> 3) & 0b11;
    switch (defaultOperator)
    {
        case 0:
            m_pageDefaultCompositionOperator = PDFJBIG2BitOperation::Or;
            break;

        case 1:
            m_pageDefaultCompositionOperator = PDFJBIG2BitOperation::And;
            break;

        case 2:
            m_pageDefaultCompositionOperator = PDFJBIG2BitOperation::Xor;
            break;

        case 3:
            m_pageDefaultCompositionOperator = PDFJBIG2BitOperation::NotXor;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    const uint32_t correctedWidth = width;
    const uint32_t correctedHeight = (height != 0xFFFFFFFF) ? height : 0;
    m_pageSizeUndefined = height == 0xFFFFFFFF;

    checkBitmapSize(correctedWidth);
    checkBitmapSize(correctedHeight);

    m_pageBitmap = PDFJBIG2Bitmap(correctedWidth, correctedHeight, m_pageDefaultPixelValue);
}

void PDFJBIG2Decoder::processEndOfPage(const PDFJBIG2SegmentHeader& header)
{
    if (header.getSegmentDataLength() != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 end-of-page segment shouldn't contain any data, but has extra data of %1 bytes.").arg(header.getSegmentDataLength()));
    }

    // We will write a warning, because end-of-page segments should not be in PDF according to specification
    m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 end-of-page segment detected and ignored."));
}

void PDFJBIG2Decoder::processEndOfStripe(const PDFJBIG2SegmentHeader& header)
{
    // Just skip the segment, do nothing
    skipSegment(header);
}

void PDFJBIG2Decoder::processEndOfFile(const PDFJBIG2SegmentHeader& header)
{
    if (header.getSegmentDataLength() != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 end-of-file segment shouldn't contain any data, but has extra data of %1 bytes.").arg(header.getSegmentDataLength()));
    }

    // We will write a warning, because end-of-file segments should not be in PDF according to specification
    m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 end-of-file segment detected and ignored."));
}

void PDFJBIG2Decoder::processProfiles(const PDFJBIG2SegmentHeader& header)
{
    skipSegment(header);
}

void PDFJBIG2Decoder::processCodeTables(const PDFJBIG2SegmentHeader& header)
{
    const uint8_t flags = m_reader.readUnsignedByte();
    const int32_t htLow = m_reader.readSignedInt();
    const int32_t htHigh = m_reader.readSignedInt();

    if (htLow == std::numeric_limits<int32_t>::min())
    {
        // Check for underflow, we subtract 1 from htLow value
        throw PDFException(PDFTranslationContext::tr("JBIG2 underflow of the low value in huffman table."));
    }

    const bool hasOOB = flags & 0x01;
    const PDFBitReader::Value htps = ((flags >> 1) & 0b111) + 1;
    const PDFBitReader::Value htrs = ((flags >> 4) & 0b111) + 1;

    std::vector<PDFJBIG2HuffmanTableEntry> table;
    table.reserve(32);

    // Read standard values
    int32_t currentRangeLow = htLow;
    while (currentRangeLow < htHigh)
    {
        PDFJBIG2HuffmanTableEntry entry;
        entry.prefixBitLength = m_reader.read(htps);
        entry.rangeBitLength = m_reader.read(htrs);
        entry.value = currentRangeLow;
        currentRangeLow += 1 << entry.rangeBitLength;
        table.push_back(entry);
    }

    // Read "low" value
    PDFJBIG2HuffmanTableEntry lowEntry;
    lowEntry.prefixBitLength = m_reader.read(htps);
    lowEntry.rangeBitLength = 32;
    lowEntry.value = htLow - 1;
    lowEntry.type = PDFJBIG2HuffmanTableEntry::Type::Negative;
    table.push_back(lowEntry);

    // Read "high" value
    PDFJBIG2HuffmanTableEntry highEntry;
    highEntry.prefixBitLength = m_reader.read(htps);
    highEntry.rangeBitLength = 32;
    highEntry.value = htHigh;
    table.push_back(highEntry);

    // Read out-of-band value, if we have it
    if (hasOOB)
    {
        PDFJBIG2HuffmanTableEntry oobEntry;
        oobEntry.prefixBitLength = m_reader.read(htps);
        oobEntry.type = PDFJBIG2HuffmanTableEntry::Type::OutOfBand;
        table.push_back(oobEntry);
    }

    table = PDFJBIG2HuffmanCodeTable::buildPrefixes(table);
    m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2HuffmanCodeTable>(qMove(table));
}

void PDFJBIG2Decoder::processExtension(const PDFJBIG2SegmentHeader& header)
{
    // We will read the extension header, and check "Necessary bit"
    const uint32_t extensionHeader = m_reader.readUnsignedInt();
    if (extensionHeader & 0x8000000)
    {
        const uint32_t extensionCode = extensionHeader & 0x3FFFFFFF;
        throw PDFException(PDFTranslationContext::tr("JBIG2 unknown extension %1 necessary for decoding the image.").arg(extensionCode));
    }

    if (header.isSegmentDataLengthDefined())
    {
        m_reader.skipBytes(header.getSegmentDataLength() - 4);
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 segment with unknown extension has not defined length."));
    }
}

PDFJBIG2Bitmap PDFJBIG2Decoder::getBitmap(const uint32_t segmentIndex, bool remove)
{
    PDFJBIG2Bitmap result;

    auto it = m_segments.find(segmentIndex);
    if (it != m_segments.cend())
    {
        PDFJBIG2Bitmap* bitmap = it->second->asBitmap();

        if (!bitmap)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 segment %1 is not a bitmap.").arg(segmentIndex));
        }

        if (remove)
        {
            result = qMove(*bitmap);
            m_segments.erase(it);
        }
        else
        {
            result = *bitmap;
        }

        return result;
    }

    throw PDFException(PDFTranslationContext::tr("JBIG2 bitmap segment %1 not found.").arg(segmentIndex));
    return result;
}

PDFJBIG2Bitmap PDFJBIG2Decoder::readBitmap(const PDFJBIG2BitmapDecodingParameters& parameters)
{
    if (parameters.MMR)
    {
        // Use modified-modified-read (it corresponds to CCITT 2D encoding)
        PDFCCITTFaxDecoderParameters ccittParameters;
        ccittParameters.K = -1;
        ccittParameters.columns = parameters.GBW;
        ccittParameters.rows = parameters.GBH;
        ccittParameters.hasEndOfBlock = false;
        ccittParameters.decode = { 1.0, 0.0 };
        ccittParameters.hasBlackIsOne = true;

        PDFCCITTFaxDecoder decoder(&parameters.data, ccittParameters);
        PDFImageData data = decoder.decode();

        PDFJBIG2Bitmap bitmap(data.getWidth(), data.getHeight(), m_pageDefaultPixelValue);

        // Copy the data
        PDFBitReader reader(&data.getData(), data.getBitsPerComponent());
        for (unsigned int row = 0; row < data.getHeight(); ++row)
        {
            for (unsigned int column = 0; column < data.getWidth(); ++column)
            {
                bitmap.setPixel(column, row, (reader.read()) ? 0xFF : 0x00);
            }

            reader.alignToBytes();
        }

        return bitmap;
    }
    else
    {
        // Use arithmetic encoding. For templates, we fill bytes from right to left, from bottom to top bits,
        // filling from lowest bit to highest bit. We will have a maximum of 16 bits.

        uint8_t LTP = 0;
        uint16_t LTPContext = 0;
        if (parameters.TPGDON)
        {
            switch (parameters.GBTEMPLATE)
            {
                case 0:
                    LTPContext = 0b1010010011011001; // 16-bit context, hexadecimal value is 0x9B25
                    break;

                case 1:
                    LTPContext = 0b0011110010101; // 13-bit context, hexadecimal value is 0x0795
                    break;

                case 2:
                    LTPContext = 0b0011100101; // 10-bit context, hexadecimal value is 0x00E5
                    break;

                case 3:
                    LTPContext = 0b0110010101; // 10-bit context, hexadecimal value is 0x0195
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }
        }

        Q_ASSERT(parameters.arithmeticDecoder);
        PDFJBIG2ArithmeticDecoder& decoder = *parameters.arithmeticDecoder;

        PDFJBIG2Bitmap bitmap(parameters.GBW, parameters.GBH, 0x00);
        for (int y = 0; y < parameters.GBH; ++y)
        {
            // Check TPGDON prediction - if we use same pixels as in previous line
            if (parameters.TPGDON)
            {
                LTP = LTP ^ decoder.readBit(LTPContext, parameters.arithmeticDecoderState);
                if (LTP)
                {
                    if (y > 0)
                    {
                        bitmap.copyRow(y, y - 1);
                    }
                    continue;
                }
            }

            for (int x = 0; x < parameters.GBW; ++x)
            {
                // Check, if we have to skip pixel. Pixel should be set to 0, but it is done
                // in the initialization of the bitmap.
                if (parameters.SKIP && parameters.SKIP->getPixelSafe(x, y))
                {
                    continue;
                }

                uint16_t pixelContext = 0;
                uint16_t pixelContextShift = 0;
                auto createContextBit = [&](int offsetX, int offsetY)
                {
                    uint16_t bit = bitmap.getPixelSafe(offsetX, offsetY) ? 1 : 0;
                    bit = bit << pixelContextShift;
                    pixelContext |= bit;
                    ++pixelContextShift;
                };

                // Create pixel context based on used template
                switch (parameters.GBTEMPLATE)
                {
                    case 0:
                    {
                        // 16-bit context
                        createContextBit(x - 1, y);
                        createContextBit(x - 2, y);
                        createContextBit(x - 3, y);
                        createContextBit(x - 4, y);
                        createContextBit(x + parameters.ATXY[0].x, y + parameters.ATXY[0].y);
                        createContextBit(x + 2, y - 1);
                        createContextBit(x + 1, y - 1);
                        createContextBit(x + 0, y - 1);
                        createContextBit(x - 1, y - 1);
                        createContextBit(x - 2, y - 1);
                        createContextBit(x + parameters.ATXY[1].x, y + parameters.ATXY[1].y);
                        createContextBit(x + parameters.ATXY[2].x, y + parameters.ATXY[2].y);
                        createContextBit(x + 1, y - 2);
                        createContextBit(x + 0, y - 2);
                        createContextBit(x - 1, y - 2);
                        createContextBit(x + parameters.ATXY[3].x, y + parameters.ATXY[3].y);
                        break;
                    }

                    case 1:
                    {
                        // 13-bit context
                        createContextBit(x - 1, y);
                        createContextBit(x - 2, y);
                        createContextBit(x - 3, y);
                        createContextBit(x + parameters.ATXY[0].x, y + parameters.ATXY[0].y);
                        createContextBit(x + 2, y - 1);
                        createContextBit(x + 1, y - 1);
                        createContextBit(x + 0, y - 1);
                        createContextBit(x - 1, y - 1);
                        createContextBit(x - 2, y - 1);
                        createContextBit(x + 2, y - 2);
                        createContextBit(x + 1, y - 2);
                        createContextBit(x + 0, y - 2);
                        createContextBit(x - 1, y - 2);
                        break;
                    }

                    case 2:
                    {
                        // 10-bit context
                        createContextBit(x - 1, y);
                        createContextBit(x - 2, y);
                        createContextBit(x + parameters.ATXY[0].x, y + parameters.ATXY[0].y);
                        createContextBit(x + 1, y - 1);
                        createContextBit(x + 0, y - 1);
                        createContextBit(x - 1, y - 1);
                        createContextBit(x - 2, y - 1);
                        createContextBit(x + 1, y - 2);
                        createContextBit(x + 0, y - 2);
                        createContextBit(x - 1, y - 2);
                        break;
                    }

                    case 3:
                    {
                        // 10-bit context
                        createContextBit(x - 1, y);
                        createContextBit(x - 2, y);
                        createContextBit(x - 3, y);
                        createContextBit(x - 4, y);
                        createContextBit(x + parameters.ATXY[0].x, y + parameters.ATXY[0].y);
                        createContextBit(x + 1, y - 1);
                        createContextBit(x + 0, y - 1);
                        createContextBit(x - 1, y - 1);
                        createContextBit(x - 2, y - 1);
                        createContextBit(x - 3, y - 1);
                        break;
                    }

                    default:
                    {
                        Q_ASSERT(false);
                        break;
                    }
                }

                bitmap.setPixel(x, y, (decoder.readBit(pixelContext, parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
            }
        }

        return bitmap;
    }

    return PDFJBIG2Bitmap();
}

PDFJBIG2Bitmap PDFJBIG2Decoder::readRefinementBitmap(const PDFJBIG2BitmapRefinementDecodingParameters& parameters)
{
    // Use algorithm described in 6.3.5.6
    PDFJBIG2Bitmap GRREG(parameters.GRW, parameters.GRH, 0x00);

    // Use arithmetic encoding. For templates, we fill bytes from right to left, from bottom to top bits,
    // filling from lowest bit to highest bit. We will have a maximum of 13 bits.

    uint32_t LTP = 0;
    const uint32_t LTPContext = !parameters.GRTEMPLATE ? 0b0000100000000 : 0b0010000000;

    PDFBitReader reader(m_reader.getStream(), 1);
    PDFJBIG2ArithmeticDecoder decoder(&reader);
    decoder.initialize();

    auto createContext = [&](int x, int y) -> uint16_t
    {
        uint16_t pixelContext = 0;
        uint16_t pixelContextShift = 0;
        auto createContextBit = [&](const PDFJBIG2Bitmap* bitmap, int offsetX, int offsetY)
        {
            uint16_t bit = bitmap->getPixelSafe(offsetX, offsetY) ? 1 : 0;
            bit = bit << pixelContextShift;
            pixelContext |= bit;
            ++pixelContextShift;
        };

        if (!parameters.GRTEMPLATE)
        {
            // 13-bit context
            createContextBit(&GRREG, x - 1, y);
            createContextBit(&GRREG, x + 1, y - 1);
            createContextBit(&GRREG, x + 0, y - 1);
            createContextBit(&GRREG, x + parameters.GRAT[0].x, y + parameters.GRAT[0].y);

            const int refX = x - parameters.GRREFERENCEX;
            const int refY = y - parameters.GRREFERENCEY;

            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY - 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY - 1);
            createContextBit(parameters.GRREFERENCE, refX + parameters.GRAT[1].x, refY + parameters.GRAT[1].y);
        }
        else
        {
            // 10-bit context
            createContextBit(&GRREG, x - 1, y);
            createContextBit(&GRREG, x + 1, y - 1);
            createContextBit(&GRREG, x + 0, y - 1);
            createContextBit(&GRREG, x - 1, y - 1);

            const int refX = x - parameters.GRREFERENCEX;
            const int refY = y - parameters.GRREFERENCEY;

            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY - 1);
        }

        return pixelContext;
    };

    auto evaluateTPGRPIX = [&](int x, int y, uint8_t& value) -> bool
    {
        const int refX = x - parameters.GRREFERENCEX;
        const int refY = y - parameters.GRREFERENCEY;

        value = parameters.GRREFERENCE->getPixelSafe(refX, refY);

        return parameters.GRREFERENCE->getPixelSafe(refX - 1, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 0, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX - 1, refY + 0) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY + 0) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX - 1, refY + 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 0, refY + 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY + 1) == value;
    };

    for (int32_t y = 0; y < static_cast<int32_t>(parameters.GRH); ++y)
    {
        if (parameters.TPGRON)
        {
            LTP = LTP ^ decoder.readBit(LTPContext, parameters.arithmeticDecoderState);
            if (LTP)
            {

            }
        }

        if (!LTP)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(parameters.GRW); ++x)
            {
                GRREG.setPixel(x, y, (decoder.readBit(createContext(x, y), parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
            }
        }
        else
        {
            for (int32_t x = 0; x < static_cast<int32_t>(parameters.GRW); ++x)
            {
                uint8_t TPGRVAL = 0;
                if (evaluateTPGRPIX(x, y, TPGRVAL))
                {
                    GRREG.setPixel(x, y, TPGRVAL);
                }
                else
                {
                    GRREG.setPixel(x, y, (decoder.readBit(createContext(x, y), parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
                }
            }
        }
    }

    return GRREG;
}

PDFJBIG2RegionSegmentInformationField PDFJBIG2Decoder::readRegionSegmentInformationField()
{
    PDFJBIG2RegionSegmentInformationField result;

    result.width = m_reader.readUnsignedInt();
    result.height = m_reader.readUnsignedInt();
    result.offsetX = m_reader.readSignedInt();
    result.offsetY = m_reader.readSignedInt();

    // Parse flags
    const uint8_t flags = m_reader.readUnsignedByte();

    if ((flags & 0b11111000) != 0)
    {
        // This is forbidden by the specification
        throw PDFException(PDFTranslationContext::tr("JBIG2 region segment information flags are invalid."));
    }

    switch (flags)
    {
        case 0:
            result.operation = PDFJBIG2BitOperation::Or;
            break;

        case 1:
            result.operation = PDFJBIG2BitOperation::And;
            break;

        case 2:
            result.operation = PDFJBIG2BitOperation::Xor;
            break;

        case 3:
            result.operation = PDFJBIG2BitOperation::NotXor;
            break;

        case 4:
            result.operation = PDFJBIG2BitOperation::Replace;
            break;

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 region segment information - invalid bit operation mode."));
    }

    checkRegionSegmentInformationField(result);
    return result;
}

PDFJBIG2ATPositions PDFJBIG2Decoder::readATTemplatePixelPositions(int count)
{
    PDFJBIG2ATPositions result = { };

    for (int i = 0; i < count; ++i)
    {
        result[i].x = m_reader.readSignedByte();
        result[i].y = m_reader.readSignedByte();
    }

    return result;
}

void PDFJBIG2Decoder::resetArithmeticStatesGeneric(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    uint8_t bits = 0;
    switch (templateMode)
    {
        case 0:
            bits = 16;
            break;

        case 1:
            bits = 13;
            break;

        case 2:
        case 3:
            bits = 10;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (!state)
    {
        m_arithmeticDecoderStates[Generic].reset(bits);
    }
    else
    {
        m_arithmeticDecoderStates[Generic].reset(bits, *state);
    }
}

void PDFJBIG2Decoder::resetArithmeticStatesGenericRefinement(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    uint8_t bits = 0;
    switch (templateMode)
    {
        case 0:
            bits = 13;
            break;

        case 1:
            bits = 10;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (!state)
    {
        m_arithmeticDecoderStates[Refinement].reset(bits);
    }
    else
    {
        m_arithmeticDecoderStates[Refinement].reset(bits, *state);
    }
}

void PDFJBIG2Decoder::skipSegment(const PDFJBIG2SegmentHeader& header)
{
    if (header.isSegmentDataLengthDefined())
    {
        m_reader.skipBytes(header.getSegmentDataLength());
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 segment with unknown data length can't be skipped."));
    }
}

PDFJBIG2ReferencedSegments PDFJBIG2Decoder::getReferencedSegments(const PDFJBIG2SegmentHeader& header) const
{
    PDFJBIG2ReferencedSegments segments;

    for (const uint32_t referredSegmentId : header.getReferredSegments())
    {
        auto it = m_segments.find(referredSegmentId);
        if (it != m_segments.cend() && it->second)
        {
            const PDFJBIG2Segment* referredSegment = it->second.get();
            if (const PDFJBIG2Bitmap* bitmap = referredSegment->asBitmap())
            {
                segments.bitmaps.push_back(bitmap);
            }
            else if (const PDFJBIG2HuffmanCodeTable* huffmanCodeTable = referredSegment->asHuffmanCodeTable())
            {
                segments.codeTables.push_back(huffmanCodeTable);
            }
            else if (const PDFJBIG2SymbolDictionary* symbolDictionary = referredSegment->asSymbolDictionary())
            {
                segments.symbolDictionaries.push_back(symbolDictionary);
            }
            else
            {
                Q_ASSERT(false);
            }
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment %1 referenced by segment %2.").arg(referredSegmentId).arg(header.getSegmentNumber()));
        }
    }

    return segments;
}

void PDFJBIG2Decoder::checkBitmapSize(const uint32_t size)
{
    if (size > MAX_BITMAP_SIZE)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum bitmap size exceeded (%1 > %2).").arg(size).arg(MAX_BITMAP_SIZE));
    }
}

void PDFJBIG2Decoder::checkRegionSegmentInformationField(const PDFJBIG2RegionSegmentInformationField& field)
{
    checkBitmapSize(field.width);
    checkBitmapSize(field.height);
    checkBitmapSize(field.offsetX);
    checkBitmapSize(field.offsetY);

    if (field.width == 0 || field.height == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid bitmap size (%1 x %2).").arg(field.width).arg(field.height));
    }

    if (field.operation == PDFJBIG2BitOperation::Invalid)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid bit operation."));
    }
}

int32_t PDFJBIG2Decoder::checkInteger(std::optional<int32_t> value)
{
    if (value.has_value())
    {
        return *value;
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 can't read integer."));
    }

    return 0;
}

PDFJBIG2Bitmap::PDFJBIG2Bitmap() :
    m_width(0),
    m_height(0)
{

}

PDFJBIG2Bitmap::PDFJBIG2Bitmap(int width, int height) :
    m_width(width),
    m_height(height)
{
    m_data.resize(width * height, 0);
}

PDFJBIG2Bitmap::PDFJBIG2Bitmap(int width, int height, uint8_t fill) :
    m_width(width),
    m_height(height)
{
    m_data.resize(width * height, fill);
}

PDFJBIG2Bitmap::~PDFJBIG2Bitmap()
{

}

PDFJBIG2Bitmap PDFJBIG2Bitmap::getSubbitmap(int offsetX, int offsetY, int width, int height) const
{
    PDFJBIG2Bitmap result(width, height, 0x00);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            result.setPixel(x, y, getPixelSafe(x + offsetX, y + offsetY));
        }
    }

    return result;
}

void PDFJBIG2Bitmap::paint(const PDFJBIG2Bitmap& bitmap, int offsetX, int offsetY, PDFJBIG2BitOperation operation, bool expandY, const uint8_t expandPixel)
{
    if (!bitmap.isValid())
    {
        return;
    }

    // Expand, if it is allowed and target bitmap has too low height
    if (expandY && offsetY + bitmap.getHeight() > m_height)
    {
        m_height = offsetY + bitmap.getHeight();
        m_data.resize(getPixelCount(), expandPixel);
    }

    // Check out pathological cases
    if (offsetX >= m_width || offsetY >= m_height)
    {
        return;
    }

    const int targetStartX = offsetX;
    const int targetEndX = qMin(offsetX + bitmap.getWidth(), m_width);
    const int targetStartY = offsetY;
    const int targetEndY = qMin(offsetY + bitmap.getHeight(), m_height);

    for (int targetY = targetStartY; targetY < targetEndY; ++targetY)
    {
        for (int targetX = targetStartX; targetX < targetEndX; ++targetX)
        {
            const int sourceX = targetX - targetStartX;
            const int sourceY = targetY - targetStartY;

            switch (operation)
            {
                case PDFJBIG2BitOperation::Or:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) | bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::And:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) & bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::Xor:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) ^ bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::NotXor:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) ^ (~bitmap.getPixel(sourceX, sourceY)));
                    break;

                case PDFJBIG2BitOperation::Replace:
                    setPixel(targetX, targetY, bitmap.getPixel(sourceX, sourceY));
                    break;

                default:
                    throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap paint operation."));
            }
        }
    }
}

void PDFJBIG2Bitmap::copyRow(int target, int source)
{
    if (target < 0 || target >= m_height || source < 0 || source >= m_height)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap copy row operation."));
    }

    auto itSource = std::next(m_data.cbegin(), source * m_width);
    auto itSourceEnd = std::next(itSource, m_width);
    auto itTarget = std::next(m_data.begin(), target * m_width);
    std::copy(itSource, itSourceEnd, itTarget);
}

PDFJBIG2HuffmanCodeTable::PDFJBIG2HuffmanCodeTable(std::vector<PDFJBIG2HuffmanTableEntry>&& entries) :
    m_entries(qMove(entries))
{

}

PDFJBIG2HuffmanCodeTable::~PDFJBIG2HuffmanCodeTable()
{

}

std::vector<PDFJBIG2HuffmanTableEntry> PDFJBIG2HuffmanCodeTable::buildPrefixes(const std::vector<PDFJBIG2HuffmanTableEntry>& entries)
{
    std::vector<PDFJBIG2HuffmanTableEntry> result = entries;
    result.erase(std::remove_if(result.begin(), result.end(), [](const PDFJBIG2HuffmanTableEntry& entry) { return entry.prefixBitLength == 0; }), result.end());
    std::stable_sort(result.begin(), result.end(), [](const PDFJBIG2HuffmanTableEntry& l, const PDFJBIG2HuffmanTableEntry& r) { return l.prefixBitLength < r.prefixBitLength; });

    if (!result.empty())
    {
        result[0].prefix = 0;

        // Strategy: we will have variable prefix containing actual prefix value. If we are changing
        // the number of bits, then we must update "FIRSTCODE" variable as in the specification, i.e.
        // compute FIRSTCODE[current bit length] = (FIRSTCODE[previous bit length] + #number of items) * 2.
        // Number of items is automatically computed by incrementing the variable prefix, so at the end
        // of each cycle, when we are about to shift number of bits in next cycle, we have computed
        // variable (FIRSTCODE[last bit length] + #number of items), so in next cycle, we just do a bit shift.
        uint16_t prefix = 1;
        uint16_t count = 1;
        for (uint32_t i = 1; i < result.size(); ++i)
        {
            const uint16_t bitShift = result[i].prefixBitLength - result[i - 1].prefixBitLength;
            if (bitShift > 0)
            {
                // Bit length of the prefix changed, we must shift the prefix by amount of new bits
                prefix = prefix << bitShift;
                count = 0;
            }

            result[i].prefix = prefix;
            ++prefix;
            ++count;

            if (count > (1 << result[i].prefixBitLength))
            {
                // We have "overflow" of values, for binary number with prefixBitLength digits (0/1), we can
                // have only 2^prefixBitLength values, which we exceeded. This is unrecoverable error.
                throw PDFException(PDFTranslationContext::tr("JBIG2 overflow of prefix bit values in huffman table."));
            }
        }
    }

    return result;
}

uint32_t PDFJBIG2ArithmeticDecoderState::getQe(size_t context) const
{
    return JBIG2_ARITHMETIC_DECODER_QE_VALUES[getQeRowIndex(context)].Qe;
}

PDFJBIG2Segment::~PDFJBIG2Segment()
{

}

PDFJBIG2HuffmanDecoder::PDFJBIG2HuffmanDecoder(PDFBitReader* reader, const PDFJBIG2HuffmanCodeTable* table) :
    m_reader(reader)
{
    m_entries = table->getEntries();
    if (!m_entries.empty())
    {
        m_begin = m_entries.data();
        m_end = m_entries.data() + m_entries.size();
    }
}

PDFJBIG2HuffmanDecoder::PDFJBIG2HuffmanDecoder(PDFBitReader* reader, std::vector<PDFJBIG2HuffmanTableEntry>&& table) :
    m_reader(reader),
    m_entries(qMove(table))
{
    if (!m_entries.empty())
    {
        m_begin = m_entries.data();
        m_end = m_entries.data() + m_entries.size();
    }
}

bool PDFJBIG2HuffmanDecoder::isOutOfBandSupported() const
{
    if (!isValid())
    {
        return false;
    }

    for (auto it = m_begin; it != m_end; ++it)
    {
        if (it->isOutOfBand())
        {
            return true;
        }
    }

    return false;
}

std::optional<int32_t> PDFJBIG2HuffmanDecoder::readSignedInteger()
{
    uint32_t prefixBitCount = 0;
    uint32_t prefix = 0;

    for (const PDFJBIG2HuffmanTableEntry* it = m_begin; it != m_end; ++it)
    {
        // Align prefix with current bit value
        Q_ASSERT(prefixBitCount <= it->prefixBitLength);
        while (prefixBitCount < it->prefixBitLength)
        {
            prefix = (prefix << 1) | m_reader->read(1);
            ++prefixBitCount;
        }

        if (prefix == it->prefix)
        {
            // We have found value. Now, there are three cases:
            //  1) Out of band value
            //  2) Negative value
            //  3) Standard value
            if (it->isOutOfBand())
            {
                return std::nullopt;
            }
            else if (it->isLowValue())
            {
                return it->value - m_reader->read(32);
            }
            else if (it->rangeBitLength == 0)
            {
                return it->value;
            }
            else
            {
                return it->value + m_reader->read(it->rangeBitLength);
            }
        }
    }

    return std::nullopt;
}

std::vector<const PDFJBIG2Bitmap*> PDFJBIG2ReferencedSegments::getSymbolBitmaps() const
{
    std::vector<const PDFJBIG2Bitmap*> result;

    for (const PDFJBIG2SymbolDictionary* dictionary  : symbolDictionaries)
    {
        const std::vector<PDFJBIG2Bitmap>& bitmaps = dictionary->getBitmaps();
        result.reserve(result.size() + bitmaps.size());
        for (const PDFJBIG2Bitmap& bitmap : bitmaps)
        {
            result.push_back(&bitmap);
        }
    }

    return result;
}

PDFJBIG2HuffmanDecoder PDFJBIG2ReferencedSegments::getUserTable(PDFBitReader* reader)
{
    if (currentUserCodeTableIndex < codeTables.size())
    {
        return PDFJBIG2HuffmanDecoder(reader, codeTables[currentUserCodeTableIndex++]);
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
    }

    return PDFJBIG2HuffmanDecoder();
}

}   // namespace pdf
