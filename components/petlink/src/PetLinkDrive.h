#pragma once
#include <stdint.h>

namespace petlink {
// 保留协议合法范围，不再额外压缩速度、裁掉横轴或限制差速。
constexpr float kRingLinearScale=0.001f;
constexpr float kRingAngularScale=0.001f;
inline int16_t driveClamp(int32_t value) {
  return int16_t(value< -1000 ? -1000 : value>1000 ? 1000 : value);
}
struct DriveIntent {
  int16_t linear,angular;
  DriveIntent(int16_t forward=0,int16_t turn=0) : linear(forward),angular(turn) {}
};
inline DriveIntent directDriveIntent(int16_t linear,int16_t angular) {
  return DriveIntent(driveClamp(linear),driveClamp(angular));
}
class DriveIntentFilter {
 public:
  void reset() { output_=pending_=0; pendingSamples_=0; }
  DriveIntent pause() const { return DriveIntent(); }
  DriveIntent update(int16_t linear,int16_t angular) {
    linear=driveClamp(linear); angular=driveClamp(angular);
    if(!angular) { output_=pending_=0; pendingSamples_=0; return DriveIntent(linear,0); }
    if(!output_ || (output_>0)==(angular>0)) {
      output_=angular; pending_=0; pendingSamples_=0;
      return DriveIntent(linear,output_);
    }
    if((pending_>0)!=(angular>0)) { pending_=angular; pendingSamples_=1; }
    else if(pendingSamples_<kSignConfirmSamples) ++pendingSamples_;
    if(pendingSamples_>=kSignConfirmSamples) { output_=pending_; pending_=0; pendingSamples_=0; }
    return DriveIntent(linear,output_);
  }
  int16_t angular() const { return output_; }
 private:
  static constexpr uint8_t kSignConfirmSamples=4; // 40 ms at the 10 ms ring worker period。
  int16_t output_=0,pending_=0;
  uint8_t pendingSamples_=0;
};
} // namespace petlink
