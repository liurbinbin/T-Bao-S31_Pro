#include "PetLocale.h"

#include <stddef.h>
#include <string.h>

namespace pet {
namespace {
struct Translation {
  const char* english;
  const char* chinese;
};
struct CjkGlyph {
  uint32_t codepoint;
  uint16_t rows[10];
};

#include "LocaleData.inc"
}  // namespace

const char* translate(const char* english, Language language) {
  if (!english || language == Language::English) return english;
  size_t low = 0;
  size_t high = sizeof(kTranslations) / sizeof(kTranslations[0]);
  while (low < high) {
    const size_t mid = low + (high - low) / 2;
    const int compared = strcmp(english, kTranslations[mid].english);
    if (compared == 0) return kTranslations[mid].chinese;
    if (compared < 0) high = mid;
    else low = mid + 1;
  }
  return english;
}

const uint16_t* chineseGlyph(uint32_t codepoint) {
  size_t low = 0;
  size_t high = sizeof(kChineseGlyphs) / sizeof(kChineseGlyphs[0]);
  while (low < high) {
    const size_t mid = low + (high - low) / 2;
    if (kChineseGlyphs[mid].codepoint == codepoint) return kChineseGlyphs[mid].rows;
    if (codepoint < kChineseGlyphs[mid].codepoint) high = mid;
    else low = mid + 1;
  }
  return nullptr;
}
}  // namespace pet
