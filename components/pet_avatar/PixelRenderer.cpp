#include "PixelRenderer.h"
#include "GroundGeometry.h"
#include "CompanionTheme.h"

#include <cstring>
#include <cstdio>

namespace pet {
namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8u) << 8) |
                               ((g & 0xFCu) << 3) | (b >> 3));
}

constexpr uint16_t kInk = rgb(54, 62, 67);
constexpr uint16_t kInkSoft = rgb(91, 99, 98);
constexpr uint16_t kCream = rgb(252, 244, 218);
constexpr uint16_t kPaper = rgb(250, 239, 213);
constexpr uint16_t kWall = rgb(245, 224, 198);
constexpr uint16_t kWallShade = rgb(235, 205, 183);
constexpr uint16_t kFloor = rgb(215, 186, 159);
constexpr uint16_t kFloorLine = rgb(196, 163, 142);
constexpr uint16_t kMint = rgb(177, 216, 158);
constexpr uint16_t kMintLight = rgb(210, 235, 184);
constexpr uint16_t kMintDark = rgb(90, 138, 91);
constexpr uint16_t kLeaf = rgb(117, 174, 104);
constexpr uint16_t kLeafLight = rgb(166, 207, 126);
constexpr uint16_t kPink = rgb(239, 159, 158);
constexpr uint16_t kPinkLight = rgb(250, 204, 190);
constexpr uint16_t kBlue = rgb(141, 190, 207);
constexpr uint16_t kBlueLight = rgb(205, 229, 230);
constexpr uint16_t kGold = rgb(232, 188, 87);
constexpr uint16_t kWhite = rgb(255, 252, 239);
constexpr uint16_t kToast = rgb(62, 72, 72);
constexpr uint16_t kRed = rgb(207, 101, 99);

struct Canvas {
  explicit Canvas(uint16_t* target,int w=kWidth,int h=kHeight,int line=0) : p(target),width(w),height(h),stride(line ? line : w) {}
  uint16_t* p;
  int width=kWidth,height=kHeight,stride=kWidth,rasterScale=1;
  Language language = Language::Chinese;
  int transformX = 0;
  int transformY = 0;
  uint16_t transformScale = 100;

  void setTransform(int cx, int cy, uint16_t scale) {
    transformX = cx;
    transformY = cy;
    transformScale = scale == 0 ? 1 : scale;
  }

  int mapX(int x) const {
    return transformX + (x - transformX) * static_cast<int>(transformScale) / 100;
  }
  int mapY(int y) const {
    return transformY + (y - transformY) * static_cast<int>(transformScale) / 100;
  }
  int mapSize(int size) const {
    if (size <= 0) return 0;
    const int result = size * static_cast<int>(transformScale) / 100;
    return result < 1 ? 1 : result;
  }
  void rawPixel(int x, int y, uint16_t color) {
    if(!p || x<0 || y<0 || x>=width/rasterScale || y>=height/rasterScale) return;
    for(int yy=0;yy<rasterScale;++yy)
      for(int xx=0;xx<rasterScale;++xx) p[(y*rasterScale+yy)*stride+x*rasterScale+xx]=color;
  }

  void pixel(int x, int y, uint16_t color) {
    rawPixel(mapX(x), mapY(y), color);
  }

  void rect(int x, int y, int w, int h, uint16_t color) {
    if (!p || w <= 0 || h <= 0) return;
    if (transformScale == 100) {
      int x0 = x < 0 ? 0 : x;
      int y0 = y < 0 ? 0 : y;
      int x1 = x + w;
      int y1 = y + h;
      if (x1 > width/rasterScale) x1 = width/rasterScale;
      if (y1 > height/rasterScale) y1 = height/rasterScale;
      if (x0 >= x1 || y0 >= y1) return;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) rawPixel(xx, yy, color);
      return;
    }
    const int x0 = mapX(x);
    const int y0 = mapY(y);
    const int x1 = mapX(x + w - 1) + 1;
    const int y1 = mapY(y + h - 1) + 1;
    for (int yy = y0; yy < y1; ++yy)
      for (int xx = x0; xx < x1; ++xx) rawPixel(xx, yy, color);
  }

  void frame(int x, int y, int w, int h, uint16_t color) {
    rect(x, y, w, 1, color);
    rect(x, y + h - 1, w, 1, color);
    rect(x, y, 1, h, color);
    rect(x + w - 1, y, 1, h, color);
  }

  void roundRect(int x, int y, int w, int h, int r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 1) {
      rect(x, y, w, h, color);
      return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    rect(x + r, y, w - r * 2, h, color);
    rect(x, y + r, w, h - r * 2, color);
    for (int yy = 0; yy < r; ++yy) {
      for (int xx = 0; xx < r; ++xx) {
        const int dx = r - xx;
        const int dy = r - yy;
        if (dx * dx + dy * dy <= r * r + r) {
          pixel(x + xx, y + yy, color);
          pixel(x + w - 1 - xx, y + yy, color);
          pixel(x + xx, y + h - 1 - yy, color);
          pixel(x + w - 1 - xx, y + h - 1 - yy, color);
        }
      }
    }
  }

  void disc(int cx, int cy, int radius, uint16_t color) {
    if (radius < 0) return;
    const int rr = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (x * x + y * y <= rr) pixel(cx + x, cy + y, color);
      }
    }
  }

  // 逻辑像素按 2 × 2 绘制，保持整数缩放的像素边缘。
  void pixelDisc(int cx, int cy, int radius, uint16_t color) {
    const int rr = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (x * x + y * y <= rr) rect(cx + x * 2, cy + y * 2, 2, 2, color);
      }
    }
  }

  void line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dyAbs = y1 > y0 ? y1 - y0 : y0 - y1;
    int dy = -dyAbs;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      pixel(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = err * 2;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
};

void glyph(char ch, uint8_t out[7]) {
  std::memset(out, 0, 7);
  if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
#define G(a, b, c, d, e, f, g)                  \
  do {                                           \
    const uint8_t rows[7] = {a, b, c, d, e, f, g}; \
    std::memcpy(out, rows, 7);                   \
    return;                                      \
  } while (0)
  switch (ch) {
    case 'A': G(14, 17, 17, 31, 17, 17, 17);
    case 'B': G(30, 17, 17, 30, 17, 17, 30);
    case 'C': G(14, 17, 16, 16, 16, 17, 14);
    case 'D': G(30, 17, 17, 17, 17, 17, 30);
    case 'E': G(31, 16, 16, 30, 16, 16, 31);
    case 'F': G(31, 16, 16, 30, 16, 16, 16);
    case 'G': G(14, 17, 16, 23, 17, 17, 14);
    case 'H': G(17, 17, 17, 31, 17, 17, 17);
    case 'I': G(14, 4, 4, 4, 4, 4, 14);
    case 'J': G(7, 2, 2, 2, 18, 18, 12);
    case 'K': G(17, 18, 20, 24, 20, 18, 17);
    case 'L': G(16, 16, 16, 16, 16, 16, 31);
    case 'M': G(17, 27, 21, 21, 17, 17, 17);
    case 'N': G(17, 25, 21, 19, 17, 17, 17);
    case 'O': G(14, 17, 17, 17, 17, 17, 14);
    case 'P': G(30, 17, 17, 30, 16, 16, 16);
    case 'Q': G(14, 17, 17, 17, 21, 18, 13);
    case 'R': G(30, 17, 17, 30, 20, 18, 17);
    case 'S': G(15, 16, 16, 14, 1, 1, 30);
    case 'T': G(31, 4, 4, 4, 4, 4, 4);
    case 'U': G(17, 17, 17, 17, 17, 17, 14);
    case 'V': G(17, 17, 17, 17, 17, 10, 4);
    case 'W': G(17, 17, 17, 21, 21, 21, 10);
    case 'X': G(17, 17, 10, 4, 10, 17, 17);
    case 'Y': G(17, 17, 10, 4, 4, 4, 4);
    case 'Z': G(31, 1, 2, 4, 8, 16, 31);
    case '0': G(14, 17, 19, 21, 25, 17, 14);
    case '1': G(4, 12, 4, 4, 4, 4, 14);
    case '2': G(14, 17, 1, 2, 4, 8, 31);
    case '3': G(30, 1, 1, 14, 1, 1, 30);
    case '4': G(2, 6, 10, 18, 31, 2, 2);
    case '5': G(31, 16, 16, 30, 1, 1, 30);
    case '6': G(14, 16, 16, 30, 17, 17, 14);
    case '7': G(31, 1, 2, 4, 8, 8, 8);
    case '8': G(14, 17, 17, 14, 17, 17, 14);
    case '9': G(14, 17, 17, 15, 1, 1, 14);
    case '"': G(10, 10, 10, 0, 0, 0, 0);
    case '#': G(10, 31, 10, 10, 31, 10, 0);
    case '$': G(4, 15, 20, 14, 5, 30, 4);
    case '&': G(12, 18, 20, 8, 21, 18, 13);
    case '\'': G(4, 4, 8, 0, 0, 0, 0);
    case '(': G(2, 4, 8, 8, 8, 4, 2);
    case ')': G(8, 4, 2, 2, 2, 4, 8);
    case '*': G(0, 21, 14, 31, 14, 21, 0);
    case ',': G(0, 0, 0, 0, 0, 4, 8);
    case ';': G(0, 4, 0, 0, 4, 4, 8);
    case '=': G(0, 0, 31, 0, 31, 0, 0);
    case '@': G(14, 17, 23, 21, 23, 16, 14);
    case '[': G(14, 8, 8, 8, 8, 8, 14);
    case '\\': G(16, 8, 8, 4, 2, 2, 1);
    case ']': G(14, 2, 2, 2, 2, 2, 14);
    case '^': G(4, 10, 17, 0, 0, 0, 0);
    case '|': G(4, 4, 4, 4, 4, 4, 4);
    case '~': G(0, 0, 9, 22, 0, 0, 0);
    case '?': G(14, 17, 1, 2, 4, 0, 4);
    case '!': G(4, 4, 4, 4, 4, 0, 4);
    case '.': G(0, 0, 0, 0, 0, 0, 4);
    case ':': G(0, 4, 0, 0, 4, 0, 0);
    case '-': G(0, 0, 0, 31, 0, 0, 0);
    case '+': G(0, 4, 4, 31, 4, 4, 0);
    case '%': G(17, 2, 4, 4, 8, 16, 17);
    case '/': G(1, 2, 2, 4, 8, 8, 16);
    case '<': G(2, 4, 8, 16, 8, 4, 2);
    case '>': G(8, 4, 2, 1, 2, 4, 8);
    case '_': G(0, 0, 0, 0, 0, 0, 31);
    default: return;
  }
#undef G
}

int textLength(const char* value, int maxChars = 64) {
  return utf8Length(value, maxChars);
}

int textWidth(const Canvas& c, const char* value, int maxChars = 64) {
  return utf8Width(translate(value, c.language), maxChars);
}

void text(Canvas& c, int x, int y, const char* value, uint16_t color,
          int scale = 1, int maxChars = 64) {
  if (!value || scale < 1) return;
  value = translate(value, c.language);
  for (int n = 0; *value && n < maxChars; ++n) {
    const uint32_t codepoint = nextCodepoint(value);
    if (codepoint < 0x80U) {
      uint8_t rows[7];
      glyph(static_cast<char>(codepoint), rows);
      for (int yy = 0; yy < 7; ++yy)
        for (int xx = 0; xx < 5; ++xx)
          if (rows[yy] & (1u << (4 - xx)))
            c.rect(x + xx * scale, y + yy * scale, scale, scale, color);
    } else {
      const uint16_t* rows = chineseGlyph(codepoint);
      if (rows) {
        for (int yy = 0; yy < 10; ++yy)
          for (int xx = 0; xx < 10; ++xx)
            if (rows[yy] & (1u << (9 - xx)))
              c.rect(x + xx * scale, y + yy * scale, scale, scale, color);
      } else {
        c.frame(x, y, 9 * scale, 9 * scale, color);
      }
    }
    x += codepointWidth(codepoint) * scale;
  }
}

void centeredText(Canvas& c, int centerX, int y, const char* value,
                  uint16_t color, int scale = 1, int maxChars = 64) {
  const int width = textWidth(c, value, maxChars) * scale;
  text(c, centerX - width / 2, y, value, color, scale, maxChars);
}

void unsignedText(uint32_t value, char out[11]) {
  char reversed[10];
  int n = 0;
  do {
    reversed[n++] = static_cast<char>('0' + value % 10u);
    value /= 10u;
  } while (value && n < 10);
  int i = 0;
  while (n) out[i++] = reversed[--n];
  out[i] = '\0';
}

void appendText(char* out, int capacity, int& used, const char* value) {
  if (!out || capacity <= 0 || !value) return;
  while (*value && used + 1 < capacity) out[used++] = *value++;
  out[used] = '\0';
}

void ratioText(uint32_t current, uint32_t required, const char* suffix,
               Language language, char out[24]) {
  char number[11];
  int used = 0;
  out[0] = '\0';
  unsignedText(current, number);
  appendText(out, 24, used, number);
  appendText(out, 24, used, "/");
  unsignedText(required, number);
  appendText(out, 24, used, number);
  appendText(out, 24, used, translate(suffix, language));
}

uint32_t progressPercent(uint32_t current, uint32_t required) {
  if (required == 0) return 100;
  if (current >= required) return 100;
  return static_cast<uint32_t>((static_cast<uint64_t>(current) * 100u) /
                               required);
}

bool matureStage(Stage value) {
  return value == Stage::Adult || value == Stage::Companion ||
         value == Stage::Centennial;
}

void arrowBack(Canvas& c) {
  c.line(12, 9, 6, 13, kInk);
  c.line(6, 13, 12, 17, kInk);
  c.rect(7, 12, 9, 3, kInk);
}

void topBar(Canvas& c, const char* title) {
  c.rect(0, 0, 128, 20, kWall);
  c.rect(0, 19, 128, 1, kWallShade);
  arrowBack(c);
  centeredText(c, 68, 7, title, kInk);
}

void heart(Canvas& c, int x, int y, uint16_t color) {
  c.rect(x + 1, y, 3, 2, color);
  c.rect(x + 6, y, 3, 2, color);
  c.rect(x, y + 2, 10, 3, color);
  c.rect(x + 1, y + 5, 8, 2, color);
  c.rect(x + 3, y + 7, 4, 2, color);
  c.rect(x + 4, y + 9, 2, 1, color);
}

void star(Canvas& c, int x, int y, uint16_t color) {
  c.rect(x + 3, y, 2, 8, color);
  c.rect(x, y + 3, 8, 2, color);
  c.rect(x + 1, y + 1, 6, 6, color);
  c.pixel(x, y, color);
  c.pixel(x + 7, y, color);
  c.pixel(x, y + 7, color);
  c.pixel(x + 7, y + 7, color);
}

void drawBattery(Canvas& c, const View& view) {
  const int x = 105;
  const int y = 6;
  c.frame(x, y, 18, 9, kInk);
  c.rect(x + 18, y + 3, 2, 3, kInk);
  if (view.batteryMv != 0) {
    uint32_t level = 0;
    if (view.batteryMv > 3300) level = view.batteryMv - 3300;
    if (level > 900) level = 900;
    const int fill = static_cast<int>((level * 14u) / 900u);
    c.rect(x + 2, y + 2, fill, 5,
           level < 180 ? kRed : (view.charging ? kGold : kMintDark));
    char volts[5];
    volts[0] = static_cast<char>('0' + (view.batteryMv / 1000u) % 10u);
    volts[1] = '.';
    volts[2] = static_cast<char>('0' + (view.batteryMv / 100u) % 10u);
    volts[3] = 'V';
    volts[4] = '\0';
    text(c, 79, 7, volts, kInkSoft);
    if (view.charging) {
      c.line(112, 6, 109, 11, kWhite);
      c.line(109, 11, 113, 11, kWhite);
      c.line(113, 11, 110, 15, kWhite);
    }
  } else {
    text(c, 95, 7, "?", kInk);
  }
}

void drawRoom(Canvas& c, bool night, bool lightOff, int cameraX,
              uint32_t animationMs) {
  const uint16_t wall = night ? rgb(103, 114, 126) : kWall;
  const uint16_t floor = night ? rgb(104, 91, 91) : kFloor;
  c.rect(0, 18, 128, 65, wall);
  c.rect(0, 83, 128, 25, floor);
  c.rect(0, 18, 4, 90, night ? rgb(82, 91, 105) : kWallShade);
  c.rect(124, 18, 4, 90, night ? rgb(82, 91, 105) : kWallShade);
  c.rect(0, 81, 128, 3, night ? rgb(82, 91, 105) : kFloorLine);
  c.rect(0, 105, 128, 3, night ? rgb(82, 91, 105) : kFloorLine);
  for (int world = -32; world < 352; world += 24) {
    const int x = world - cameraX;
    c.line(x, 107, x + 20, 84, kFloorLine);
  }

  const int windowX = 28 - cameraX;
  c.roundRect(windowX, 27, 31, 27, 3, kWhite);
  c.rect(windowX + 3, 30, 25, 20, night ? rgb(58, 81, 111) : kBlueLight);
  c.rect(windowX + 15, 30, 2, 20, kWhite);
  c.rect(windowX + 3, 39, 25, 2, kWhite);
  if (night) {
    c.disc(windowX + 8, 34, 3, kGold);
    c.pixel(windowX + 23, 34, kWhite);
    c.pixel(windowX + 21, 45, kWhite);
  } else {
    c.disc(windowX + 8, 34, 3, kGold);
    c.rect(windowX - 1, 26, 5, 29, kPinkLight);
    c.rect(windowX + 27, 26, 5, 29, kPinkLight);
  }

  const int cloudX = 14 + ((static_cast<int>(animationMs / 90U) + cameraX) % 92) - cameraX;
  if (!night) {
    c.roundRect(cloudX, 57, 13, 4, 3, kWhite);
    c.disc(cloudX + 4, 56, 4, kWhite);
    c.disc(cloudX + 9, 57, 3, kWhite);
  }

  // 世界中段的厨房柜台，向右移动时才进入视野。
  const int kitchenX = 132 - cameraX;
  c.roundRect(kitchenX, 42, 38, 26, 4, rgb(218, 173, 126));
  c.rect(kitchenX + 4, 47, 30, 12, kCream);
  c.rect(kitchenX + 9, 51, 8, 5, kPinkLight);
  c.rect(kitchenX + 22, 51, 8, 5, kMintLight);
  c.rect(kitchenX + 4, 65, 30, 3, rgb(153, 102, 83));

  const int bedX = 245 - cameraX;
  c.roundRect(bedX, 72, 32, 19, 4, rgb(177, 136, 125));
  c.roundRect(bedX + 2, 69, 15, 8, 3, kCream);
  c.roundRect(bedX + 3, 76, 27, 12, 3, kPinkLight);
  c.rect(bedX + 3, 88, 3, 8, kInkSoft);
  c.rect(bedX + 27, 88, 3, 8, kInkSoft);
  centeredText(c, bedX + 16, 80, "TUCK", kInkSoft);

  const int plantX = 48 - cameraX;
  c.rect(plantX, 75, 2, 10, kMintDark);
  c.line(plantX + 1, 79, plantX - 4, 74, kMintDark);
  c.line(plantX + 1, 79, plantX + 7, 73, kMintDark);
  c.disc(plantX - 4, 73, 3, kLeaf);
  c.disc(plantX + 8, 72, 3, kLeafLight);
  c.roundRect(plantX - 6, 84, 14, 11, 3, rgb(205, 135, 105));
  c.rect(plantX - 7, 82, 16, 4, rgb(224, 153, 112));

  const int chestX = 8 - cameraX;
  c.roundRect(chestX, 82, 24, 16, 3, rgb(172, 119, 84));
  c.rect(chestX, 86, 24, 3, kGold);
  c.rect(chestX + 10, 86, 5, 7, kInkSoft);
  c.rect(chestX + 12, 88, 1, 3, kGold);

  const int bookX = 74 - cameraX;
  c.roundRect(bookX - 2, 36, 28, 28, 2, rgb(166, 116, 86));
  c.rect(bookX, 39, 24, 10, kWallShade);
  c.rect(bookX, 52, 24, 9, kWallShade);
  for (int i = 0; i < 4; ++i) {
    c.rect(bookX + i * 6, 39 + i % 2, 4, 9 - i % 2,
           i % 2 ? kPink : kBlue);
    c.rect(bookX + i * 6, 53, 4, 7, i % 2 ? kMintDark : kGold);
  }

  const int clockX = 114 - cameraX;
  c.disc(clockX, 38, 10, rgb(166, 116, 86));
  c.disc(clockX, 38, 8, kCream);
  c.line(clockX, 38, clockX, 33, kInk);
  const int hand = ((animationMs / 1000U) & 1U) ? 4 : 3;
  c.line(clockX, 38, clockX + hand, 40, kInk);

  const int toyX = 100 - cameraX;
  c.disc(toyX, 94, 8, kPink);
  c.line(toyX - 6, 90, toyX + 6, 98, kCream);
  c.line(toyX - 5, 99, toyX + 5, 89, kCream);
  c.disc(toyX - 2, 91, 2, kWhite);

  const int sofaX = 174 - cameraX;
  c.roundRect(sofaX, 71, 28, 20, 4, kMintDark);
  c.roundRect(sofaX + 3, 73, 22, 10, 3, kMintLight);
  c.roundRect(sofaX - 2, 80, 7, 12, 3, kLeaf);
  c.roundRect(sofaX + 23, 80, 7, 12, 3, kLeaf);
  c.rect(sofaX + 5, 91, 3, 4, kInkSoft);
  c.rect(sofaX + 21, 91, 3, 4, kInkSoft);
  c.roundRect(sofaX + 13, 76, 8, 7, 2, kPinkLight);

  const int tankX = 206 - cameraX;
  c.roundRect(tankX, 39, 27, 21, 3, kInkSoft);
  c.rect(tankX + 2, 42, 23, 15, kBlueLight);
  c.rect(tankX + 3, 54, 21, 3, kGold);
  const int fishX = tankX + 6 + static_cast<int>((animationMs / 350U) % 11U);
  c.disc(fishX, 48, 3, kPink);
  c.line(fishX + 2, 48, fishX + 5, 45, kPink);
  c.line(fishX + 2, 48, fishX + 5, 51, kPink);
  c.pixel(fishX - 1, 47, kInk);
  c.rect(tankX + 20, 49, 2, 7, kMintDark);
  c.pixel(tankX + 9, 44, kWhite);
  c.pixel(tankX + 19, 43, kWhite);

  const int rugX = 211 - cameraX;
  c.roundRect(rugX, 90, 29, 14, 5, kPinkLight);
  c.frame(rugX + 4, 93, 21, 8, kPink);

  const int lampX = 289 - cameraX;
  c.rect(lampX - 1, 55, 2, 30, kInkSoft);
  c.rect(lampX - 6, 85, 12, 2, kInkSoft);
  c.roundRect(lampX - 8, 44, 16, 12, 3,
              lightOff ? kInkSoft : (night ? kGold : kPinkLight));
  c.rect(lampX - 6, 41, 12, 4, lightOff ? kWallShade : kCream);
  c.roundRect(lampX - 8, 63, 16, 11, 3, lightOff ? kInkSoft : kCream);
  centeredText(c, lampX, 65, lightOff ? "ON" : "OFF",
               lightOff ? kCream : kInkSoft);

  const int pictureX = 176 - cameraX;
  c.frame(pictureX, 29, 21, 24, kMintDark);
  c.rect(pictureX + 2, 31, 17, 20, kCream);
  heart(c, pictureX + 5, 36, kPink);

  // 世界最右侧的花园门是移动到尽头后才会出现的隐藏景物。
  const int doorX = 304 - cameraX;
  c.roundRect(doorX, 36, 24, 48, 4, kMintDark);
  c.roundRect(doorX + 4, 40, 16, 40, 3, kLeafLight);
  c.disc(doorX + 15, 61, 2, kGold);
}

void drawLeaf(Canvas& c, int cx, int baseY, int facing, Reaction reaction,
              Nature natureValue, Stage stageValue) {
  const int side = facing < 0 ? -1 : 1;
  int tipX = cx;
  int tipY = baseY - 15;
  if (reaction == Reaction::Sleeping) {
    tipX = cx + side * 9;
    tipY = baseY - 7;
  } else if (reaction == Reaction::Happy || reaction == Reaction::Heart) {
    tipX = cx - side * 8;
    tipY = baseY - 13;
  } else if (reaction == Reaction::Dizzy) {
    tipX = cx + side * 3;
    tipY = baseY - 10;
  } else if (reaction == Reaction::Thinking || natureValue == Nature::Clever) {
    tipX = cx + side * 3;
    tipY = baseY - 17;
  } else if (natureValue == Nature::Explorer) {
    tipX = cx + side * 8;
    tipY = baseY - 13;
  }

  c.line(cx, baseY + 2, tipX, tipY + 5, kMintDark);
  c.line(cx + 1, baseY + 2, tipX + 1, tipY + 5, kMintDark);
  c.disc(tipX - 4, tipY + 3, 6, kMintDark);
  c.disc(tipX + 4, tipY + 2, 6, kMintDark);
  c.disc(tipX - 4, tipY + 2, 4, kLeaf);
  c.disc(tipX + 4, tipY + 1, 4, kLeafLight);
  c.line(tipX, tipY + 1, tipX, tipY + 7, kMintDark);

  if (stageValue == Stage::Adult || stageValue == Stage::Companion ||
      stageValue == Stage::Centennial) {
    c.disc(tipX + 7, tipY - 2, 2, kPink);
    c.disc(tipX + 3, tipY - 4, 2, kCream);
    c.disc(tipX + 4, tipY, 2, kGold);
  }
  if (stageValue == Stage::Centennial) star(c, tipX - 14, tipY - 7, kGold);
}

void drawFormBack(Canvas& c, int cx, int cy, int radius, int facing,
                  Form formValue, bool mature) {
  const int side = facing < 0 ? -1 : 1;
  const int edge = radius * 2;
  if (formValue == Form::Leaf) {
    c.line(cx - side * edge, cy + 8, cx - side * (edge + 9), cy + 3,
           kMintDark);
    c.disc(cx - side * (edge + 11), cy, mature ? 5 : 4, kLeaf);
    c.line(cx - side * (edge + 14), cy + 3,
           cx - side * (edge + 8), cy - 3, kMintDark);
  } else if (formValue == Form::Dew) {
    const int tx = cx - side * (edge + 10);
    c.line(cx - side * edge, cy + 9, tx + side * 6, cy + 5, kBlue);
    c.line(tx + side * 6, cy + 5, tx, cy + 10, kBlue);
    c.line(tx, cy + 10, tx - side * 5, cy + 5, kBlue);
    c.disc(cx - edge + 2, cy - radius - 4, mature ? 6 : 5, kBlue);
    c.line(cx - edge + 2, cy - radius - 13,
           cx - edge - 3, cy - radius - 5, kBlue);
    c.line(cx - edge + 2, cy - radius - 13,
           cx - edge + 7, cy - radius - 5, kBlue);
    c.disc(cx + edge - 2, cy - radius - 4, mature ? 6 : 5, kBlue);
    c.line(cx + edge - 2, cy - radius - 13,
           cx + edge - 7, cy - radius - 5, kBlue);
    c.line(cx + edge - 2, cy - radius - 13,
           cx + edge + 3, cy - radius - 5, kBlue);
  } else if (formValue == Form::Fruit) {
    c.disc(cx - edge + 2, cy - radius - 5, mature ? 7 : 6, kPink);
    c.disc(cx + edge - 2, cy - radius - 5, mature ? 7 : 6, kPink);
    c.disc(cx - edge + 1, cy - radius - 6, mature ? 4 : 3, kPinkLight);
    c.disc(cx + edge - 1, cy - radius - 6, mature ? 4 : 3, kPinkLight);
    c.line(cx - side * edge, cy + 10, cx - side * (edge + 8), cy + 13,
           kRed);
    c.disc(cx - side * (edge + 11), cy + 12, mature ? 5 : 4, kPink);
    c.line(cx - side * (edge + 11), cy + 7,
           cx - side * (edge + 7), cy + 4, kMintDark);
  }
}

void drawFormFront(Canvas& c, int cx, int cy, Form formValue, bool mature) {
  if (formValue == Form::Leaf) {
    const int petal = mature ? 5 : 4;
    c.disc(cx - 12, cy + 16, petal, kLeaf);
    c.disc(cx, cy + 19, petal, kLeafLight);
    c.disc(cx + 12, cy + 16, petal, kLeaf);
    c.line(cx - 16, cy + 12, cx + 16, cy + 12, kMintDark);
  } else if (formValue == Form::Dew) {
    c.line(cx - 15, cy + 15, cx - 8, cy + 19, kBlue);
    c.line(cx - 8, cy + 19, cx, cy + 15, kBlue);
    c.line(cx, cy + 15, cx + 8, cy + 19, kBlue);
    c.line(cx + 8, cy + 19, cx + 15, cy + 15, kBlue);
    c.disc(cx, cy - 15, mature ? 3 : 2, kBlue);
    c.line(cx, cy - 22, cx - 3, cy - 16, kBlue);
    c.line(cx, cy - 22, cx + 3, cy - 16, kBlue);
  } else if (formValue == Form::Fruit) {
    c.roundRect(cx - (mature ? 12 : 10), cy + 12,
                mature ? 25 : 21, mature ? 11 : 9, 5, kPinkLight);
    c.frame(cx - (mature ? 12 : 10), cy + 12,
            mature ? 25 : 21, mature ? 11 : 9, kPink);
    c.disc(cx, cy + 16, 3, kGold);
    c.line(cx, cy - 20, cx + 4, cy - 25, kMintDark);
    c.disc(cx + 7, cy - 26, 3, kLeaf);
  }
}

void drawNatureAccessory(Canvas& c, int cx, int cy, Nature natureValue) {
  if (natureValue == Nature::Explorer) {
    c.line(cx - 16, cy + 14, cx + 15, cy + 18, kGold);
    c.rect(cx + 13, cy + 17, 8, 4, kGold);
  } else if (natureValue == Nature::Cuddly) {
    heart(c, cx - 5, cy + 15, kPink);
  } else if (natureValue == Nature::Clever) {
    c.frame(cx - 15, cy - 5, 11, 10, kInkSoft);
    c.frame(cx + 5, cy - 5, 11, 10, kInkSoft);
    c.rect(cx - 4, cy - 2, 9, 2, kInkSoft);
  }
}

void drawCareStyle(Canvas& c, int cx, int cy, uint32_t careStyle) {
  if (careStyle == 1) {
    // 照顾装饰不能取代主形态的轮廓特征。
    c.disc(cx + 19, cy - 18, 2, kPink);
    c.disc(cx + 24, cy - 18, 2, kPinkLight);
    c.disc(cx + 21, cy - 22, 2, kCream);
    c.disc(cx + 21, cy - 18, 2, kGold);
    star(c, cx - 25, cy - 20, kGold);
  } else if (careStyle == 2) {
    c.disc(cx, cy + 19, 2, kGold);
    c.line(cx - 2, cy + 19, cx - 10, cy + 15, kPink);
    c.line(cx - 2, cy + 19, cx - 10, cy + 23, kPink);
    c.line(cx + 2, cy + 19, cx + 10, cy + 15, kPink);
    c.line(cx + 2, cy + 19, cx + 10, cy + 23, kPink);
    c.disc(cx - 8, cy + 19, 4, kPinkLight);
    c.disc(cx + 8, cy + 19, 4, kPinkLight);
  } else if (careStyle == 3) {
    // 失误只改变可爱配件，不使用丑化或惩罚性外观。
    c.disc(cx - 18, cy - 18, 4, kCream);
    c.disc(cx - 22, cy - 15, 4, kMintLight);
    c.disc(cx - 16, cy - 13, 4, kLeafLight);
    c.line(cx - 17, cy - 10, cx - 22, cy - 18, kMintDark);
    c.disc(cx - 23, cy - 20, 3, kLeaf);
    c.disc(cx - 18, cy - 22, 3, kLeafLight);
  }
}

void drawSleepBlanket(Canvas& c, const View& view) {
  if (view.sleepTransition != 0 && view.sleepProgress < 75) return;
  const int cx = view.petX;
  const int cy = view.petY;
  c.roundRect(cx - 14, cy + 3, 28, 10, 5, kPinkLight);
  c.roundRect(cx - 11, cy + 1, 20, 7, 4, kCream);
  c.line(cx + 6, cy + 4, cx + 10, cy + 9, kPink);
  text(c, cx + 16, cy - 15, "Z", kBlue);
  text(c, cx + 23, cy - 23, "Z", kBlue);
}

void drawPet(Canvas& c, const State& state, const View& view) {
  Stage stageValue = stage(state);
  Nature natureValue = nature(state);
  Reaction reaction = view.reaction;
  if (view.sleepTransition != 0 && view.sleepProgress < 75 &&
      reaction == Reaction::Sleeping)
    reaction = Reaction::Happy;
  if ((state.sleeping || state.nursery || isNight(state)) && reaction == Reaction::Idle)
    reaction = Reaction::Sleeping;

  const int radius = petBodyRadius(stageValue);
  const Form formValue = form(state);
  const bool mature = matureStage(stageValue);
  const uint16_t bodyOutline = formValue == Form::Dew ? rgb(91, 151, 174)
                               : formValue == Form::Fruit ? kRed
                                                         : kMintDark;
  const uint16_t bodyColor = formValue == Form::Dew ? kBlueLight
                             : formValue == Form::Fruit ? kPinkLight
                                                       : kMintLight;

  int jump = 0;
  if (reaction == Reaction::Happy || reaction == Reaction::Heart) {
    const uint32_t phase = view.animationMs % 800u;
    jump = phase < 400u ? static_cast<int>(phase / 80u)
                         : static_cast<int>((800u - phase) / 80u);
  }
  if (reaction == Reaction::Dizzy) {
    const uint32_t phase = view.animationMs % 600u;
    jump = phase < 300u ? static_cast<int>(phase / 150u)
                        : static_cast<int>((600u - phase) / 150u);
  }
  const int breathe = ((view.animationMs / 700u) & 1u) ? 1 : 0;
  int cx = view.petX;
  if(view.page!=Page::Companion) {
    if (cx < 24) cx = 24;
    if (cx > 104) cx = 104;
  }
  int scale = view.petScale;
  int footY = view.petY + petFootOffset(stageValue) * scale / 100;
  const bool onGround = view.page == Page::Home && view.scene == Scene::Room &&
                        view.sleepTransition == 0 && !state.sleeping;
  if (onGround) {
    const GroundPose pose = groundPose(view.petDepth, stageValue);
    footY = pose.footY;
    scale = pose.scale;
  }
  const int cy = footY - petFootOffset(stageValue) - jump - breathe;
  Canvas saved = c;
  const int shadowHalf = (radius * 2 + 1) * scale / 100;
  const int shadowHeight = scale < 80 ? 3 : 4;
  c.roundRect(cx - shadowHalf, footY - shadowHeight, shadowHalf * 2 + 1,
              shadowHeight, 2, rgb(184, 155, 139));
  // 以脚底而非身体中心缩放，变小或呼吸时落地点不会越过墙脚线。
  c.setTransform(cx, footY, static_cast<uint16_t>(scale));
  const int facing = view.petFacing < 0 ? -1 : 1;
  const int leafBase = cy - radius * 2 + 4;

  drawFormBack(c, cx, cy, radius, facing, formValue, mature);
  drawLeaf(c, cx, leafBase, facing, reaction, natureValue, stageValue);

  if (reaction != Reaction::Sleeping && stageValue != Stage::Seed) {
    c.roundRect(cx - 15, cy + radius * 2 - 3, 11, 7, 3, bodyOutline);
    c.roundRect(cx + 5, cy + radius * 2 - 3, 11, 7, 3, bodyOutline);
    c.roundRect(cx - 13, cy + radius * 2 - 3, 8, 5, 2, bodyColor);
    c.roundRect(cx + 6, cy + radius * 2 - 3, 8, 5, 2, bodyColor);
  }

  c.pixelDisc(cx, cy, radius + 1, bodyOutline);
  c.pixelDisc(cx, cy, radius, bodyColor);
  c.roundRect(cx - radius * 2 + 6, cy - radius + 3,
              radius * 4 - 10, radius * 2 + 11, 9, kCream);
  drawFormFront(c, cx, cy, formValue, mature);

  const bool blink = (view.animationMs % 3200u) >= 2640u &&
                     (view.animationMs % 3200u) < 2780u;
  const int eyeY = cy - 4;
  if (reaction == Reaction::Dizzy) {
    c.line(cx - 14, eyeY, cx - 7, eyeY + 7, kInk);
    c.line(cx - 7, eyeY, cx - 14, eyeY + 7, kInk);
    c.line(cx + 7, eyeY, cx + 14, eyeY + 7, kInk);
    c.line(cx + 14, eyeY, cx + 7, eyeY + 7, kInk);
    star(c, cx - 23, cy - 20, kGold);
    star(c, cx + 23, cy - 22, kGold);
  } else if (blink || reaction == Reaction::Sleeping) {
    c.rect(cx - 13, eyeY + 3, 8, 2, kInk);
    c.rect(cx + 6, eyeY + 3, 8, 2, kInk);
    if (reaction == Reaction::Sleeping) {
      c.pixel(cx - 13, eyeY + 2, kInk);
      c.pixel(cx + 13, eyeY + 2, kInk);
    }
  } else if (reaction == Reaction::Happy || reaction == Reaction::Heart) {
    c.line(cx - 14, eyeY + 4, cx - 10, eyeY, kInk);
    c.line(cx - 10, eyeY, cx - 6, eyeY + 4, kInk);
    c.line(cx + 6, eyeY + 4, cx + 10, eyeY, kInk);
    c.line(cx + 10, eyeY, cx + 14, eyeY + 4, kInk);
  } else {
    c.roundRect(cx - 13, eyeY, 7, 9, 3, kInk);
    c.roundRect(cx + 7, eyeY, 7, 9, 3, kInk);
    c.rect(cx - 11, eyeY + 1, 2, 2, kWhite);
    c.rect(cx + 9, eyeY + 1, 2, 2, kWhite);
  }

  c.roundRect(cx - 20, cy + 8, 8, 5, 2, kPinkLight);
  c.roundRect(cx + 13, cy + 8, 8, 5, 2, kPinkLight);

  if (reaction == Reaction::Eating) {
    c.roundRect(cx - 3, cy + 8, 7, 6, 3, kInk);
    c.rect(cx - 11, cy + 15, 23, 4, kBlue);
    c.roundRect(cx - 8, cy + 13, 17, 8, 3, kWhite);
    c.rect(cx - 6, cy + 14, 13, 3, kGold);
    c.pixel(cx + 19, cy - 5, kGold);
    c.rect(cx + 22, cy - 9, 2, 2, kGold);
  } else if (reaction == Reaction::Happy || reaction == Reaction::Heart) {
    c.rect(cx - 5, cy + 9, 11, 2, kInk);
    c.rect(cx - 3, cy + 11, 7, 2, kPink);
    if (reaction == Reaction::Heart) {
      heart(c, cx + 22, cy - 21, kPink);
      heart(c, cx - 29, cy - 11, kPink);
    }
  } else if (reaction == Reaction::Sleeping) {
    c.rect(cx - 4, cy + 10, 8, 2, kInkSoft);
    text(c, cx + 22, cy - 25, "Z", kBlue);
    text(c, cx + 29, cy - 34, "Z", kBlue);
  } else if (reaction == Reaction::Dizzy) {
    c.line(cx - 5, cy + 9, cx, cy + 12, kInk);
    c.line(cx, cy + 12, cx + 5, cy + 9, kInk);
    c.disc(cx - 20, cy + 17, 2, kPink);
    c.disc(cx + 20, cy + 17, 2, kPink);
  } else if (reaction == Reaction::Washing) {
    c.rect(cx - 3, cy + 10, 7, 2, kInk);
    c.disc(cx - 24, cy + 12, 4, kBlueLight);
    c.disc(cx + 22, cy + 16, 5, kWhite);
    c.disc(cx + 27, cy + 9, 3, kBlueLight);
    c.rect(cx - 2, cy + 10, 5, 2, kInk);
    c.disc(cx + 21, cy - 18, 2, kWhite);
    c.disc(cx + 27, cy - 24, 3, kWhite);
    c.roundRect(cx + 30, cy - 34, 15, 10, 4, kWhite);
    text(c, cx + 35, cy - 32, "?", kInkSoft);
  } else {
    c.rect(cx - 3, cy + 10, 7, 2, kInk);
    c.pixel(cx - 4, cy + 9, kInk);
    c.pixel(cx + 4, cy + 9, kInk);
  }

  if (view.secretEffect == 1) {
    const int bounce = static_cast<int>((view.animationMs / 120U) % 4U);
    heart(c, cx + 19, cy - 24 - bounce, kPink);
    heart(c, cx - 25, cy - 18 + bounce, kPinkLight);
  } else if (view.secretEffect == 2) {
    star(c, cx + 22, cy - 26, kGold);
    star(c, cx - 24, cy - 22, kWhite);
  } else if (view.secretEffect == 3) {
    const int orbit = static_cast<int>((view.animationMs / 120U) % 8U);
    star(c, cx - 20 + orbit, cy - 28, kGold);
    star(c, cx + 18 - orbit, cy - 20, kGold);
  }

  drawNatureAccessory(c, cx, cy, natureValue);
  drawCareStyle(c, cx, cy, state.careStyle);

  if (stageValue == Stage::Toddler) {
    c.roundRect(cx - 6, cy + 19, 13, 4, 2, kGold);
  } else if (stageValue == Stage::Teen) {
    c.disc(cx, cy + 19, 3, kMint);
  } else if (stageValue == Stage::Adult) {
    c.rect(cx - 7, cy + 18, 15, 2, kMintDark);
  } else if (stageValue == Stage::Companion) {
    c.rect(cx - 15, cy + 17, 30, 3, kPink);
    heart(c, cx - 4, cy + 18, kRed);
  } else if (stageValue == Stage::Centennial) {
    c.rect(cx - 14, cy + 17, 28, 3, kGold);
    star(c, cx - 4, cy + 18, kGold);
  }
  c = saved;
}

void homeFooter(Canvas& c, bool night = false) {
  const uint16_t floor = night ? rgb(104, 91, 91) : kFloor;
  const uint16_t ink = night ? kCream : kInk;
  c.rect(0, 108, 128, 20, floor);
  c.rect(0, 108, 128, 1, kFloorLine);
  c.rect(63, 111, 1, 14, kFloorLine);
  heart(c, 9, 113, kPink);
  text(c, 24, 115, "CARE", ink);
  c.disc(76, 118, 6, kBlue);
  text(c, 74, 115, "I", kWhite);
  text(c, 87, 115, "INFO", ink);
}

void sceneHeader(Canvas& c, const State& state, const View& view,
                 const char* label, uint16_t headerColor) {
  c.rect(0, 0, 128, 18, headerColor);
  c.rect(0, 17, 128, 1, kWallShade);
  text(c, 5, 6, label, kInk);
  char dayText[11];
  unsignedText(day(state), dayText);
  text(c, 42, 6, "D", kInkSoft);
  text(c, 54, 6, dayText, kInkSoft);
  drawBattery(c, view);
}

void renderRestaurant(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, rgb(250, 215, 180));
  c.rect(0, 68, 128, 40, rgb(185, 135, 105));
  c.rect(0, 68, 128, 4, rgb(153, 102, 83));
  sceneHeader(c, state, view, "CAFE", kPink);

  const char* foods[3] = {"LEAF", "DEW", "FRUIT"};
  const uint16_t foodColors[3] = {kLeafLight, kBlueLight, kPinkLight};
  uint8_t selected = view.mealFood > 2 ? 0 : view.mealFood;
  for (int i = 0; i < 3; ++i) {
    const int x = 2 + i * 42;
    c.roundRect(x, 22, 40, 22, 4, i == selected ? foodColors[i] : kCream);
    if (i == selected) c.frame(x + 1, 23, 38, 20, kInkSoft);
    centeredText(c, x + 20, 30, foods[i], kInk);
  }

  uint8_t bites = view.mealBites > 3 ? 3 : view.mealBites;
  char bite[4] = "0/3";
  bite[0] = static_cast<char>('0' + bites);
  text(c, 6, 48, "BITE", kInkSoft);
  text(c, 6, 60, bite, kInkSoft);

  View mealView = view;
  mealView.petX = 64;
  mealView.petY = 63;
  mealView.petScale = 62;
  mealView.reaction = view.reaction;
  drawPet(c, state, mealView);

  const uint16_t filling = selected == 0 ? kLeaf
                           : selected == 1 ? kBlue
                                           : kPink;
  c.roundRect(37, 81, 54, 18, 7, kCream);
  c.roundRect(41, 78, 46, 14, 6, filling);
  c.rect(44, 81, 40, 5, foodColors[selected]);
  c.roundRect(33, 96, 62, 6, 3, kInkSoft);
  for (int i = 0; i < bites; ++i)
    c.disc(51 + i * 13, 76 - (i & 1) * 3, 2, foodColors[selected]);
}

void renderWashroom(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, kBlueLight);
  for (int x = 0; x < 128; x += 32) c.rect(x, 18, 1, 90, kWhite);
  for (int y = 18; y < 108; y += 22) c.rect(0, y, 128, 1, kWhite);
  sceneHeader(c, state, view, "BATH", kBlueLight);

  const char* steps[6] = {"WET", "SOAP", "RUB", "RINSE", "DRY", "DONE"};
  uint8_t step = view.bathStep > 5 ? 5 : view.bathStep;
  centeredText(c, 64, 21, steps[step], kInk);
  for (int i = 0; i < 6; ++i)
    c.disc(44 + i * 8, 34, 2, i <= step ? kMintDark : kWhite);

  // 与主程序左侧肥皂点击区域保持一致。
  c.roundRect(2, 39, 33, 34, 5, step == 1 ? kPinkLight : kWhite);
  c.roundRect(9, 45, 19, 13, 5, kPink);
  c.disc(12, 43, 3, kWhite);
  c.disc(25, 40, 2, kWhite);
  centeredText(c, 18, 61, "SOAP", kInk);

  // 与主程序右侧淋浴点击区域保持一致。
  c.roundRect(91, 28, 35, 45, 5,
              (step == 0 || step == 3) ? kBlue : kWhite);
  c.rect(106, 34, 4, 15, kInkSoft);
  c.roundRect(101, 31, 14, 7, 3, kInkSoft);
  for (int i = 0; i < 4; ++i)
    c.line(102 + i * 4, 40, 98 + i * 5, 51, kBlue);
  centeredText(c, 108, 59, "SHOWER", kInk);

  // 擦洗区必须留在宠物躯干附近，不能覆盖两侧工具按钮。
  c.frame(36, 45, 54, 45, step == 2 ? kGold : kWhite);
  View bathView = view;
  bathView.petX = 64;
  bathView.petY = 63;
  bathView.petScale = 58;
  bathView.reaction = step == 5 && view.reaction == Reaction::Idle
                          ? Reaction::Happy : view.reaction;
  drawPet(c, state, bathView);
  if (step == 2) centeredText(c, 64, 79, "RUB", kInk);

  // 毛巾区域须位于擦洗区下方，并与主程序的点击范围一致。
  c.roundRect(18, 87, 92, 18, 5, step == 4 ? kGold : kCream);
  c.rect(25, 91, 78, 7, kPinkLight);
  c.line(34, 91, 34, 98, kPink);
  c.line(94, 91, 94, 98, kPink);
  centeredText(c, 64, 93, "TOWEL", kInk);
}

void renderPark(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, rgb(166, 218, 227));
  c.rect(0, 24, 128, 84, rgb(124, 188, 111));
  c.disc(18, 34, 9, kGold);
  c.roundRect(43, 30, 24, 5, 3, kWhite);
  c.disc(49, 29, 5, kWhite);
  c.disc(59, 30, 4, kWhite);
  c.rect(0, 91, 128, 17, rgb(100, 163, 92));
  c.roundRect(8, 71, 33, 8, 3, rgb(157, 103, 77));
  c.rect(13, 77, 3, 13, kInkSoft);
  c.rect(34, 77, 3, 13, kInkSoft);
  // 原池塘区域改为伙伴小车，与「联动」入口对应。
  c.roundRect(83, 70, 35, 17, 4, kBlueLight);
  c.roundRect(91, 61, 19, 12, 3, kCream);
  c.disc(90, 89, 5, kInk); c.disc(112, 89, 5, kInk);
  c.disc(90, 89, 2, kWhite); c.disc(112, 89, 2, kWhite);
  sceneHeader(c, state, view, "PARK", rgb(166, 218, 227));
  drawPet(c, state, view);
  c.roundRect(8, 98, 53, 9, 4, kLeafLight);
  centeredText(c, 34, 100, "RUN", kInk);
  c.roundRect(68, 98, 53, 9, 4, kBlueLight);
  centeredText(c, 94, 100, "LINK", kInk);
  homeFooter(c, false);
}

void renderClinic(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, rgb(232, 239, 220));
  c.rect(0, 18, 128, 90, kCream);
  c.rect(7, 22, 114, 18, kWall);
  sceneHeader(c, state, view, "CLINIC", kMintLight);

  const uint32_t masks[4] = {Overfed, Toothache, Dirty, Exhausted};
  const char* labels[4] = {"FULL", "TOOTH", "DIRTY", "TIRED"};
  for (int i = 0; i < 4; ++i) {
    const int x = 3 + i * 31;
    const bool active = (state.illness & masks[i]) != 0;
    c.roundRect(x, 23, 29, 15, 3, active ? kPinkLight : kWallShade);
    centeredText(c, x + 14, 27, labels[i], active ? kRed : kInkSoft,
                 1, 5);
  }

  View clinicView = view;
  clinicView.petX = 31;
  clinicView.petY = 69;
  clinicView.petScale = 52;
  clinicView.reaction = state.illness ? Reaction::Thinking : Reaction::Happy;
  drawPet(c, state, clinicView);

  // 药品按钮留出整块可点击区域，避免小图标难以命中。
  c.roundRect(58, 45, 66, 56, 7, state.illness ? kBlueLight : kMintLight);
  c.rect(65, 51, 10, 24, kWhite);
  c.rect(67, 48, 6, 5, kInkSoft);
  c.rect(65, 65, 10, 10, kPink);
  c.rect(82, 54, 31, 7, kWhite);
  centeredText(c, 98, 54, "MEDICINE", kInk, 1, 8);
  const uint32_t doses = state.illness ? (state.treatmentSteps > 2 ? 2 : state.treatmentSteps) : 2;
  for (int i = 0; i < 2; ++i)
    c.disc(88 + i * 15, 72, 4, i < static_cast<int>(doses) ? kMintDark : kWhite);
  char doseText[32];
  char doseNumber[4] = "0/2";
  doseNumber[0] = static_cast<char>('0' + doses);
  int doseUsed = 0;
  doseText[0] = '\0';
  appendText(doseText, sizeof(doseText), doseUsed, translate("DOSE", c.language));
  appendText(doseText, sizeof(doseText), doseUsed, " ");
  appendText(doseText, sizeof(doseText), doseUsed, doseNumber);
  centeredText(c, 91, 84, state.illness ? doseText : "ALL WELL", kInkSoft);

  centeredText(c, 91, 92,
               state.illness ? "TAP" : "HEALTHY", kInkSoft);
}

void sceneToolbar(Canvas& c, const char* hint, uint16_t color) {
  c.roundRect(99, 2, 25, 14, 4, color);
  c.line(106, 6, 117, 13, kInk);
  c.line(117, 6, 106, 13, kInk);
  c.roundRect(7, 109, 114, 16, 5, color);
  centeredText(c, 64, 113, hint, kInk);
}

void renderScene(Canvas& c, const State& state, const View& view) {
  if (view.scene == Scene::Restaurant) {
    renderRestaurant(c, state, view);
    sceneToolbar(c, "CHOOSE FOOD / BOWL", kPinkLight);
  } else if (view.scene == Scene::Washroom) {
    renderWashroom(c, state, view);
    sceneToolbar(c, "USE THE RIGHT TOOL", kBlueLight);
  } else if (view.scene == Scene::Clinic) {
    renderClinic(c, state, view);
    sceneToolbar(c, "TAP MEDICINE", kMintLight);
  } else {
    renderPark(c, state, view);
    sceneToolbar(c, "RUN OR LINK", kLeafLight);
  }
}

void drawWaste(Canvas& c, int x, int y, uint32_t count) {
  const uint16_t brown = rgb(126, 85, 65);
  c.roundRect(x - 7, y + 3, 15, 7, 3, brown);
  c.roundRect(x - 5, y, 11, 7, 3, brown);
  c.disc(x, y - 2, 3, brown);
  c.pixel(x - 2, y + 1, kCream);
  c.pixel(x + 3, y + 1, kCream);
  if (count > 1) {
    char amount[11];
    unsignedText(count, amount);
    text(c, x + 9, y - 2, amount, kInkSoft, 1, 2);
  }
}

void drawIllnessMark(Canvas& c, int x, int y) {
  c.disc(x, y, 7, kCream);
  c.frame(x - 7, y - 7, 15, 15, kRed);
  c.rect(x - 2, y - 5, 5, 11, kRed);
  c.rect(x - 5, y - 2, 11, 5, kRed);
}

void renderHome(Canvas& c, const State& state, const View& view) {
  if (view.scene == Scene::Restaurant) {
    renderRestaurant(c, state, view);
    return;
  }
  if (view.scene == Scene::Washroom) {
    renderWashroom(c, state, view);
    return;
  }
  if (view.scene == Scene::Clinic) {
    renderClinic(c, state, view);
    return;
  }
  const bool roomNight = isNight(state) || state.lightOff != 0;
  drawRoom(c, roomNight, state.lightOff != 0, view.cameraX, view.animationMs);
  c.rect(0, 0, 128, 18, roomNight ? rgb(103, 114, 126) : kWall);
  c.rect(0, 17, 128, 1, roomNight ? rgb(82, 91, 105) : kWallShade);
  char dayText[11];
  unsignedText(day(state), dayText);
  if (c.language == Language::Chinese) {
    text(c, 5, 5, "GROW", roomNight ? kCream : kMintDark);
    text(c, 31, 5, "DAY", roomNight ? kCream : kInk);
    text(c, 54, 6, dayText, roomNight ? kCream : kInkSoft);
  } else {
    text(c, 5, 1, "GROW", roomNight ? kCream : kMintDark);
    text(c, 5, 10, "DAY", roomNight ? kCream : kInk);
    text(c, 27, 10, dayText, roomNight ? kCream : kInkSoft);
  }
  drawBattery(c, view);

  if (state.poopCount > 0)
    drawWaste(c, 80 - view.cameraX, 92, state.poopCount);
  drawPet(c, state, view);
  if (state.sleeping && state.blanket != 0) drawSleepBlanket(c, view);

  const char* need = needText(state);
  if (need && need[0]) {
    char line[64];
    fitTextLine(translate(need, c.language), line, sizeof(line), 118, 20);
    const int width = utf8Width(line) + 10;
    c.roundRect((128 - width) / 2, 20, width, 12, 4,
                roomNight ? kInkSoft : kCream);
    centeredText(c, 64, 22, line, roomNight ? kCream : kInk, 1, 20);
  }
  if (state.illness != 0) drawIllnessMark(c, 115, 27);
  homeFooter(c, roomNight);
}

void iconFood(Canvas& c, int cx, int cy) {
  c.roundRect(cx - 12, cy + 3, 24, 9, 4, kPink);
  c.rect(cx - 9, cy + 1, 18, 5, kCream);
  c.disc(cx - 4, cy, 3, kGold);
  c.disc(cx + 4, cy + 1, 3, kMintDark);
}

void iconClean(Canvas& c, int cx, int cy) {
  c.disc(cx - 5, cy + 5, 7, kBlue);
  c.disc(cx + 5, cy + 4, 8, kBlueLight);
  c.disc(cx + 7, cy - 6, 4, kWhite);
  c.disc(cx - 9, cy - 5, 3, kWhite);
}

void iconPlay(Canvas& c, int cx, int cy) {
  c.disc(cx, cy + 2, 11, kPink);
  c.line(cx - 9, cy - 3, cx + 9, cy + 7, kCream);
  c.line(cx - 7, cy + 8, cx + 7, cy - 8, kCream);
  c.disc(cx - 3, cy - 5, 2, kWhite);
}

void iconSleep(Canvas& c, int cx, int cy) {
  c.disc(cx, cy, 11, kGold);
  c.disc(cx + 6, cy - 4, 11, kPaper);
  star(c, cx - 11, cy - 11, kBlue);
}

void careCard(Canvas& c, int x, int y, uint16_t color, const char* label,
              int iconKind) {
  c.roundRect(x + 3, y + 3, 58, 40, 6, color);
  if (iconKind == 0) iconFood(c, x + 32, y + 17);
  if (iconKind == 1) iconClean(c, x + 32, y + 17);
  if (iconKind == 2) iconPlay(c, x + 32, y + 17);
  if (iconKind == 3) iconSleep(c, x + 32, y + 17);
  centeredText(c, x + 32, y + 31, label, kInk);
}

void renderCare(Canvas& c, const State& state) {
  c.rect(0, 0, 128, 128, kPaper);
  topBar(c, "CARE");
  careCard(c, 0, 20, kPinkLight, "FEED", 0);
  careCard(c, 64, 20, kBlueLight, "CLEAN", 1);
  careCard(c, 0, 66, rgb(229, 205, 225), "PLAY", 2);
  careCard(c, 64, 66, rgb(226, 218, 174), state.sleeping ? "WAKE" : "SLEEP", 3);
  c.rect(0, 110, 128, 18, kMintDark);
  c.rect(63, 112, 1, 14, kMintLight);
  centeredText(c, 32, 116, state.nursery ? "COME HOME" : "NURSERY",
               kWhite, 1, 9);
  centeredText(c, 96, 116, "CLINIC", kWhite);
}

void statBar(Canvas& c, int y, const char* label, uint32_t value,
             uint16_t color) {
  text(c, 8, y, label, kInk);
  c.roundRect(48, y, 70, 8, 4, kWallShade);
  uint32_t p = percent(value);
  if (p > 100) p = 100;
  int fill = static_cast<int>((p * 68u) / 100u);
  if (fill > 0) c.roundRect(49, y + 1, fill, 6, 3, color);
}

void renderStatus(Canvas& c, const State& state) {
  c.rect(0, 0, 128, 128, kPaper);
  topBar(c, "STATUS");
  statBar(c, 27, "FOOD", state.hunger, kPink);
  statBar(c, 41, "JOY", state.joy, kGold);
  statBar(c, 55, "REST", state.energy, kBlue);
  statBar(c, 69, "CLEAN", state.hygiene, kMintDark);

  text(c, 8, 85, "BOND", kInk);
  char bondText[11];
  unsignedText(percent(state.bond), bondText);
  text(c, 43, 85, bondText, kPink);
  text(c, 59, 85, "%", kPink);
  text(c, 78, 85, stageName(stage(state)), kInkSoft, 1, 8);
  centeredText(c, 64, 98, natureName(nature(state)), kMintDark, 1, 18);

  c.rect(0, 110, 128, 18, kBlue);
  c.rect(63, 112, 1, 14, kBlueLight);
  centeredText(c, 32, 116, "GROW", kWhite);
  centeredText(c, 96, 116, "LOG", kWhite);
}

void requirementRow(Canvas& c, int y, const char* label, uint32_t current,
                    uint32_t required, const char* suffix, uint16_t color,
                    uint32_t shownPercent = 101) {
  text(c, 5, y, label, kInk, 1, 5);
  c.roundRect(35, y, 47, 7, 3, kCream);
  const uint32_t amount = shownPercent <= 100 ? shownPercent
                                              : progressPercent(current, required);
  const int fill = static_cast<int>(amount * 45u / 100u);
  if (fill > 0) c.roundRect(36, y + 1, fill, 5, 2, color);
  char ratio[24];
  ratioText(current, required, suffix, c.language, ratio);
  text(c, 85, y, ratio, kInkSoft, 1, 7);
}

void affinityRow(Canvas& c, int y, const char* label, uint32_t value,
                 uint16_t color) {
  text(c, 5, y, label, kInkSoft, 1, 5);
  c.roundRect(39, y, 58, 7, 3, kCream);
  const int fill = static_cast<int>((value > 100 ? 100 : value) * 56u / 100u);
  if (fill > 0) c.roundRect(40, y + 1, fill, 5, 2, color);
  char number[11];
  unsignedText(value > 100 ? 100 : value, number);
  text(c, 101, y, number, kInk, 1, 3);
  text(c, 119, y, "%", kInkSoft);
}

void renderGrowth(Canvas& c, const State& state) {
  c.rect(0, 0, 128, 128, kWall);
  topBar(c, "GROWTH");
  const GrowthProgress progress = growthProgress(state);

  uint32_t timeCurrent = state.ageSeconds;
  uint32_t timeRequired = progress.ageRequired;
  const char* timeSuffix = "S";
  if (timeRequired >= kDay) {
    timeCurrent /= kDay;
    timeRequired = (timeRequired + kDay - 1u) / kDay;
    timeSuffix = "D";
  } else if (timeRequired >= 60u) {
    timeCurrent /= 60u;
    timeRequired = (timeRequired + 59u) / 60u;
    timeSuffix = "M";
  }
  requirementRow(c, 23, "TIME", timeCurrent, timeRequired, timeSuffix, kBlue,
                 progressPercent(state.ageSeconds, progress.ageRequired));
  requirementRow(c, 33, "CARE", state.activeDays, progress.daysRequired, "D",
                 kMintDark);
  requirementRow(c, 43, "MEAL", state.feedCount, progress.mealsRequired, "",
                 kPink);
  requirementRow(c, 53, "PLAY", state.playCount, progress.gamesRequired, "",
                 kGold);
  requirementRow(c, 63, "BATH", state.cleanCount, progress.bathsRequired, "",
                 kBlue);

  if (progress.complete) {
    centeredText(c, 64, 73, "GROWTH COMPLETE", kMintDark, 1, 15);
  } else {
    text(c, 5, 73, progress.ready ? "READY" : "NEXT", progress.ready ? kMintDark : kInkSoft);
    text(c, 42, 73, stageName(progress.next), kInk, 1, 13);
  }

  const uint64_t affinityTotal = static_cast<uint64_t>(state.leafAffinity) +
                                 state.dewAffinity + state.fruitAffinity;
  uint32_t leafRatio = 0;
  uint32_t dewRatio = 0;
  uint32_t fruitRatio = 0;
  if (affinityTotal != 0) {
    leafRatio = static_cast<uint32_t>(
        static_cast<uint64_t>(state.leafAffinity) * 100u / affinityTotal);
    dewRatio = static_cast<uint32_t>(
        static_cast<uint64_t>(state.dewAffinity) * 100u / affinityTotal);
    fruitRatio = 100u - leafRatio - dewRatio;
  }
  affinityRow(c, 83, "LEAF", leafRatio, kLeaf);
  affinityRow(c, 92, "DEW", dewRatio, kBlue);
  affinityRow(c, 101, "FRUIT", fruitRatio, kPink);

  c.rect(0, 110, 128, 18, progress.ready ? kMintDark : kInkSoft);
  c.rect(63, 112, 1, 14, kCream);
  centeredText(c, 32, 116, "EVOLVE", kWhite);
  centeredText(c, 96, 116, "LOG", kWhite);
}

void logCountRow(Canvas& c, int y, const char* label, uint32_t value) {
  c.roundRect(5, y, 118, 9, 3, kCream);
  text(c, 10, y + 1, label, kInkSoft, 1, 8);
  char number[11];
  unsignedText(value, number);
  const int count = textLength(number, 10);
  text(c, 118 - count * 6, y + 1, number, kInk, 1, 10);
}

void renderCareLog(Canvas& c, const State& state) {
  c.rect(0, 0, 128, 128, kWall);
  topBar(c, "CARE LOG");
  const uint32_t stageMisses = state.careMisses >= state.stageMissBaseline
                                   ? state.careMisses - state.stageMissBaseline
                                   : 0;
  logCountRow(c, 21, "TIMELY", state.timelyResponses);
  logCountRow(c, 32, "MISSED", state.careMisses);
  logCountRow(c, 43, "MISS NOW", stageMisses);
  logCountRow(c, 54, "FEED", state.feedCount);
  logCountRow(c, 65, "CLEAN", state.cleanCount);
  logCountRow(c, 76, "POOP", state.poopCount);

  c.roundRect(5, 87, 118, 18, 3, kCream);
  text(c, 10, 93, "ILLNESS", kInkSoft);
  const uint32_t masks[4] = {Overfed, Toothache, Dirty, Exhausted};
  const char labels[4] = {'O', 'T', 'D', 'E'};
  const char* const chineseLabels[4] = {"饱", "牙", "脏", "疲"};
  for (int i = 0; i < 4; ++i) {
    const bool active = (state.illness & masks[i]) != 0;
    c.disc(76 + i * 13, 96, 5, active ? kRed : kWallShade);
    char one[2] = {labels[i], '\0'};
    centeredText(c, 76 + i * 13, 92,
                 c.language == Language::Chinese ? chineseLabels[i] : one,
                 active ? kWhite : kInkSoft);
  }

  c.rect(0, 110, 128, 18, kBlue);
  c.rect(63, 112, 1, 14, kBlueLight);
  centeredText(c, 32, 116, "MEMORY", kWhite);
  centeredText(c, 96, 116, "CLINIC", kWhite);
}

void renderMemories(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, kPaper);
  topBar(c, "MEMORY");
  uint32_t index = view.memoryIndex;
  if (index >= kMilestoneCount) index = kMilestoneCount - 1;
  const Milestone& item = milestone(index);

  c.roundRect(9, 26, 110, 73, 8, kWhite);
  c.frame(15, 32, 35, 40, kWallShade);
  c.rect(18, 35, 29, 34, kBlueLight);
  c.roundRect(26, 53, 14, 12, 5, kMintLight);
  c.rect(32, 43, 2, 12, kMintDark);
  c.disc(28, 44, 4, kLeaf);
  c.disc(37, 42, 5, kLeafLight);
  c.rect(18, 75, 29, 3, kPinkLight);

  char dayValue[11];
  unsignedText(item.day, dayValue);
  text(c, 57, 34, "DAY", kInkSoft);
  text(c, 81, 34, dayValue, kInk);
  text(c, 57, 48, item.title, kInk, 1, 10);

  const uint32_t mask = 1u << index;
  if (state.claimedMilestones & mask) {
    c.roundRect(56, 66, 54, 15, 5, kMintLight);
    centeredText(c, 83, 70, "CLAIMED", kMintDark);
  } else if ((unlockedMilestones(state) & mask) != 0) {
    c.roundRect(56, 66, 54, 15, 5, kGold);
    centeredText(c, 83, 70, "READY", kInk);
  } else {
    c.roundRect(56, 66, 54, 15, 5, kWallShade);
    centeredText(c, 83, 70, "LOCKED", kInkSoft);
  }

  char position[8];
  char number[11];
  unsignedText(index + 1, number);
  position[0] = number[0];
  position[1] = '/';
  position[2] = static_cast<char>('0' + kMilestoneCount);
  position[3] = '\0';
  centeredText(c, 64, 91, position, kInkSoft);

  c.roundRect(5, 105, 55, 19, 5, kBlueLight);
  c.roundRect(68, 105, 55, 19, 5, kBlueLight);
  centeredText(c, 32, 111, "< PREV", kInk);
  centeredText(c, 96, 111, "NEXT >", kInk);
}

void renderSettings(Canvas& c, const State& state, const View& view) {
  c.rect(0, 0, 128, 128, kPaper);
  topBar(c, "SETTINGS");

  c.roundRect(8, 25, 112, 24, 5, kWhite);
  c.disc(22, 36, 6, kGold);
  text(c, 35, 28, "BRIGHT", kInk);
  char bright[11];
  unsignedText(state.brightness, bright);
  text(c, 92, 28, bright, kInkSoft);
  c.roundRect(35, 42, 72, 4, 2, kWallShade);
  const uint32_t brightness = state.brightness > 100 ? 100 : state.brightness;
  c.roundRect(35, 42, static_cast<int>(brightness * 72U / 100U), 4, 2, kGold);

  c.roundRect(8, 52, 112, 26, 5, kMintLight);
  text(c, 15, 55, "LANGUAGE", kInk);
  text(c, 77, 55, view.language == Language::Chinese ? "中文" : "ENGLISH", kMintDark);
  centeredText(c, 64, 68, "TAP TO SWITCH", kInkSoft);

  c.roundRect(8, 81, 112, 20, 5, kWhite);
  text(c, 15, 85, "CLOCK", kInk);
  const bool trusted = view.clockKnown || state.clockTrusted != 0;
  text(c, 67, 85, trusted ? "TRUSTED" : "UNKNOWN", trusted ? kMintDark : kRed);

  centeredText(c, 64, 106, "SERIAL SET TIME", kInkSoft);
  if (view.saveError) {
    c.rect(0, 118, 128, 10, kRed);
    centeredText(c, 64, 118, "SAVE ERROR", kWhite);
  }
}

void drawDew(Canvas& c, int x, int y) {
  c.disc(x, y + 3, 5, kBlue);
  c.line(x, y - 5, x - 4, y + 2, kBlue);
  c.line(x, y - 5, x + 4, y + 2, kBlue);
  c.disc(x - 2, y + 1, 1, kWhite);
}

void renderGame(Canvas& c, const View& view) {
  c.rect(0, 0, 128, 128, rgb(173, 218, 226));
  c.rect(0, 23, 128, 82, rgb(139, 193, 117));
  c.disc(18, 34, 8, kGold);
  c.roundRect(42, 28, 22, 5, 3, kWhite);
  c.disc(48, 27, 5, kWhite);
  c.disc(57, 28, 4, kWhite);
  c.rect(0, 91, 128, 14, rgb(111, 169, 96));
  c.rect(12, 67, 3, 24, kMintDark);
  c.disc(12, 64, 12, kLeaf);
  c.disc(22, 69, 8, kLeafLight);
  c.rect(113, 67, 3, 24, kMintDark);
  c.disc(113, 64, 12, kLeaf);
  c.disc(103, 69, 8, kLeafLight);
  c.rect(0, 0, 128, 19, rgb(173, 218, 226));
  arrowBack(c);
  text(c, 18, 6, "DEW DASH", kInk);
  char score[11];
  unsignedText(view.gameScore, score);
  text(c, 74, 6, c.language == Language::Chinese ? "分" : "S", kInkSoft);
  text(c, 87, 6, score, kInk);
  char remaining[11];
  unsignedText(view.gameRemaining, remaining);
  text(c, 103, 6, remaining, kRed);

  c.roundRect(7, 23, 114, 82, 4, kMintLight);
  c.rect(44, 23, 2, 82, kLeafLight);
  c.rect(82, 23, 2, 82, kLeafLight);
  for (int y = 28; y < 102; y += 14) {
    c.rect(25, y, 2, 5, kMintLight);
    c.rect(63, y, 2, 5, kMintLight);
    c.rect(101, y, 2, 5, kMintLight);
  }

  int dropLane = view.dropLane;
  if (dropLane < 0) dropLane = 0;
  if (dropLane > 2) dropLane = 2;
  int dropY = view.dropY;
  if (dropY < 24) dropY = 24;
  if (dropY > 99) dropY = 99;
  drawDew(c, 26 + dropLane * 38, dropY);

  int lane = view.gameLane;
  if (lane < 0) lane = 0;
  if (lane > 2) lane = 2;
  const int px = 26 + lane * 38;
  c.roundRect(px - 11, 85, 23, 18, 8, kMintDark);
  c.roundRect(px - 9, 85, 19, 16, 7, kMintLight);
  c.rect(px - 6, 91, 4, 5, kInk);
  c.rect(px + 3, 91, 4, 5, kInk);
  c.rect(px - 2, 98, 5, 2, kPink);
  c.line(px, 86, px, 80, kMintDark);
  c.disc(px - 4, 81, 4, kLeaf);
  c.disc(px + 4, 80, 4, kLeafLight);

  c.roundRect(14, 110, 100, 14, 5, kToast);
  centeredText(c, 64, 114, "TILT OR TOUCH", kWhite);
}

void drawToast(Canvas& c, const View& view) {
  if (!view.toast || !view.toast[0]) return;
  const char* value = translate(view.toast, c.language);
  char first[80];
  char second[80];
  const char* rest = fitTextLine(value, first, sizeof(first), 116);
  while (rest && *rest == ' ') ++rest;
  const char* end = fitTextLine(rest, second, sizeof(second), 116);
  if (end && *end) {
    // 长对白按完整字符滚动；不能按字节截断汉字。
    const int length = utf8Length(value, 160);
    const int offset = static_cast<int>((view.animationMs / 350U) %
                                        static_cast<uint32_t>(length + 4));
    const char* start = value;
    for (int i = 0; *start && i < offset; ++i) nextCodepoint(start);
    rest = fitTextLine(start, first, sizeof(first), 116);
    while (rest && *rest == ' ') ++rest;
    fitTextLine(rest, second, sizeof(second), 116);
  }
  const bool twoLines = second[0] != '\0';
  int y = twoLines ? 80 : 91;
  if (view.page == Page::Scene) y = twoLines ? 100 : 110;
  if (view.page == Page::Game) y = twoLines ? 79 : 91;
  if (view.page == Page::Memories) y = twoLines ? 77 : 88;
  if (view.page == Page::Settings) y = twoLines ? 85 : 96;
  const int firstWidth = utf8Width(first);
  const int secondWidth = utf8Width(second);
  const int width = (firstWidth > secondWidth ? firstWidth : secondWidth) + 8;
  const bool companion=view.page==Page::Companion;
  c.roundRect((128 - width) / 2, y, width, twoLines ? 28 : 16, 5, companion ? kMintLight : kToast);
  centeredText(c, 64, y + 3, first, companion ? kInk : kWhite);
  if (twoLines) centeredText(c, 64, y + 15, second, companion ? kInk : kWhite);
}

#include "CompanionArt.inc"

#include "CompanionUi.inc"

}  // namespace

void PixelRenderer::renderAvatar(const State& state,const View& view,uint16_t* pixels) {
  if(!pixels) return;
  Canvas c(pixels); c.rect(0,0,128,128,kPaper);
  View avatar=view; avatar.page=Page::Companion; avatar.petX=64; avatar.petY=70;
  avatar.petScale=100; avatar.sleepTransition=0;
  drawPet(c,state,avatar);
}

void PixelRenderer::renderCompanionStage(const State& sprout,const CompanionFrame& frame,
    uint16_t* pixels,int width,int height,int stride) {
  if(!pixels || width<1 || width>256 || height<1 || height>128 || (stride && stride<width)) return;
  Canvas stageCanvas(pixels,width,height,stride);
  stageCanvas.rasterScale=width==256 ? 2 : 1;
  drawCompanionWorld(stageCanvas,sprout,frame);
}

void PixelRenderer::render(const State& state, const View& view,
                           uint16_t* pixels) {
  if (!pixels) return;
  Canvas canvas(pixels);
  canvas.language = view.language;
  canvas.rect(0, 0, kWidth, kHeight, kPaper);

  switch (view.page) {
    case Page::Home:
      renderHome(canvas, state, view);
      break;
    case Page::Care:
      renderCare(canvas, state);
      break;
    case Page::Status:
      renderStatus(canvas, state);
      break;
    case Page::Memories:
      renderMemories(canvas, state, view);
      break;
    case Page::Settings:
      renderSettings(canvas, state, view);
      break;
    case Page::Game:
      renderGame(canvas, view);
      break;
    case Page::Scene:
      renderScene(canvas, state, view);
      break;
    case Page::Growth:
      renderGrowth(canvas, state);
      break;
    case Page::Companion:
      renderCompanion(canvas,state,view);
      break;
    case Page::CareLog:
      renderCareLog(canvas, state);
      break;
    default:
      renderHome(canvas, state, view);
      break;
  }
  drawToast(canvas, view);
}

}  // namespace pet
