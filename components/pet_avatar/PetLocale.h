#pragma once

#include <stdint.h>

namespace pet {

enum class Language : uint8_t { Chinese = 0, English = 1 };

const char* translate(const char* english, Language language);
const uint16_t* chineseGlyph(uint32_t codepoint);

inline uint32_t nextCodepoint(const char*& text) {
  if (!text || !*text) return 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  const uint8_t lead = p[0];
  if (lead < 0x80U) { ++text; return lead; }
  unsigned count = 0;
  uint32_t value = 0;
  uint32_t minimum = 0;
  if (lead >= 0xC2U && lead <= 0xDFU) { count = 2; value = lead & 0x1FU; minimum = 0x80; }
  else if (lead >= 0xE0U && lead <= 0xEFU) { count = 3; value = lead & 0x0FU; minimum = 0x800; }
  else if (lead >= 0xF0U && lead <= 0xF4U) { count = 4; value = lead & 0x07U; minimum = 0x10000; }
  else { ++text; return '?'; }
  for (unsigned i = 1; i < count; ++i) {
    if (p[i] == 0 || (p[i] & 0xC0U) != 0x80U) { ++text; return '?'; }
    value = (value << 6U) | (p[i] & 0x3FU);
  }
  if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
    ++text;
    return '?';
  }
  text += count;
  return value;
}

inline int codepointWidth(uint32_t codepoint) { return codepoint < 0x80U ? 6 : 10; }

inline int utf8Width(const char* text, int maxCharacters = 160) {
  int width = 0;
  for (int count = 0; text && *text && count < maxCharacters; ++count)
    width += codepointWidth(nextCodepoint(text));
  return width;
}

inline int utf8Length(const char* text, int maximum = 160) {
  int count = 0;
  while (text && *text && count < maximum) { nextCodepoint(text); ++count; }
  return count;
}

// 按完整 UTF-8 字符截取，省略或换行都不能把一个汉字拆成半个。
inline const char* fitTextLine(const char* input, char* output, unsigned capacity,
                              int maxWidth, int maxCharacters = 160) {
  if (!output || capacity == 0) return input;
  unsigned used = 0;
  int width = 0;
  int count = 0;
  while (input && *input && count < maxCharacters) {
    const char* next = input;
    const uint32_t codepoint = nextCodepoint(next);
    const unsigned bytes = static_cast<unsigned>(next - input);
    const int advance = codepointWidth(codepoint);
    if (width + advance > maxWidth || used + bytes >= capacity) break;
    for (unsigned i = 0; i < bytes; ++i) output[used++] = input[i];
    width += advance;
    input = next;
    ++count;
  }
  output[used] = '\0';
  return input;
}

}  // namespace pet
