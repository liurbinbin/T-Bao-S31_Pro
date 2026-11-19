#pragma once

#include "PetModel.h"

namespace pet {

constexpr int16_t kGroundBackY = 86;
constexpr int16_t kGroundFrontY = 103;

inline int petBodyRadius(Stage value) {
  switch (value) {
    case Stage::Seed: return 9;
    case Stage::Baby:
    case Stage::Toddler: return 10;
    case Stage::Teen: return 11;
    default: return 12;
  }
}

inline int petFootOffset(Stage value) {
  // 包含身体轮廓和脚掌的最下沿，不能只按圆形身体的半径约束。
  return petBodyRadius(value) * 2 + 3;
}

struct GroundPose {
  int16_t depth;
  int16_t footY;
  int16_t centerY;
  uint8_t scale;
};

inline GroundPose groundPose(int depth, Stage value) {
  if (depth < 0) depth = 0;
  if (depth > 100) depth = 100;
  GroundPose pose;
  pose.depth = static_cast<int16_t>(depth);
  pose.scale = static_cast<uint8_t>(65 + depth * 35 / 100);
  pose.footY = static_cast<int16_t>(kGroundBackY +
      depth * (kGroundFrontY - kGroundBackY) / 100);
  pose.centerY = static_cast<int16_t>(pose.footY -
      petFootOffset(value) * pose.scale / 100);
  return pose;
}

}  // namespace pet
