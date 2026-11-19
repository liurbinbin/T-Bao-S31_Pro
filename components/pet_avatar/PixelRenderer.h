#pragma once

#include <stdint.h>

#include "PetModel.h"
#include "CompanionScene.h"

namespace pet {

// 固定输出 128 × 128 RGB565，不依赖板端或桌面图形库。
class PixelRenderer {
 public:
  void render(const State& state, const View& view, uint16_t* pixels);
  // 小车复用同一只芽伴的外观，输出 128×128 RGB565，不接管物理 LCD。
  void renderAvatar(const State& state, const View& view, uint16_t* pixels);
  void renderCompanionStage(const State& sprout,const CompanionFrame& frame,uint16_t* pixels,
                            int width,int height,int stride=0);
};

}  // namespace pet
