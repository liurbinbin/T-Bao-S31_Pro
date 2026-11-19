#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

namespace petlink {
// 此文件也随小车组件发布；线上协议不依赖结构体布局或平台字长。
constexpr size_t kHeader = 20;
constexpr size_t kTag = 16;
constexpr size_t kMaxFrame = 144;
constexpr uint32_t kControlTimeout = 300;
constexpr uint32_t kLinkTimeout = 2500;
constexpr uint32_t kInputTimeout = 180;
constexpr uint8_t kProtocolVersion = 3;
enum class InputReason : uint8_t { None, Missing, Acceleration, AngularSpeed, Inverted, Settling };
enum class StopReason : uint8_t { None, User, ControlTimeout, LinkLost, ImuStale, MotionFault, RemoteStop, Reconnecting, Unpairing, AckTimeout };
enum class UnpairState : uint8_t { None, Pending, Both, LocalOnly, Peer, Failed };
enum class Interaction : uint8_t { Wave = 1, Run, Hug, Secret, HighFive, Pet };
enum class Type : uint8_t {
  Offer = 1, Confirm, Discover, Welcome, Heartbeat, Arm, Grant,
  Control, Stop, Release, Status, Pet, Event, EventAck, Unbind, UnbindAck,
  LinkReady, LinkAck, Goodbye
};
inline void put16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
inline void put32(uint8_t* p, uint32_t n) { for (int i=0;i<4;++i) p[i] = n >> (8*i); }
inline void put64(uint8_t* p, uint64_t n) { for (int i=0;i<8;++i) p[i] = n >> (8*i); }
inline uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t* p) { uint32_t n=0; for(int i=0;i<4;++i) n |= uint32_t(p[i]) << (8*i); return n; }
inline uint64_t get64(const uint8_t* p) { uint64_t n=0; for(int i=0;i<8;++i) n |= uint64_t(p[i]) << (8*i); return n; }
inline bool newer(uint32_t a, uint32_t b) { return int32_t(a-b)>0; }
inline bool elapsed(uint32_t now, uint32_t then, uint32_t duration) { return now-then >= duration; }
struct Packet {
  Type type;
  uint64_t session;
  uint32_t seq;
  const uint8_t* data;
  uint16_t length;
  bool authenticated;
};
inline size_t header(uint8_t* p, Type type, uint64_t session, uint32_t seq,
                     uint16_t length, bool auth) {
  if (kHeader + length + (auth ? kTag : 0) > kMaxFrame) return 0;
  p[0]='P'; p[1]='R'; p[2]=kProtocolVersion; p[3]=uint8_t(type);
  put16(p+4,length); put16(p+6,auth ? 1 : 0); put64(p+8,session); put32(p+16,seq);
  return kHeader+length;
}
inline bool decode(const uint8_t* p, size_t n, Packet& out) {
  if (!p || n<kHeader || n>kMaxFrame || p[0]!='P' || p[1]!='R' || p[2]!=kProtocolVersion ||
      p[3]<uint8_t(Type::Offer) || p[3]>uint8_t(Type::Goodbye) || get16(p+6)>1) return false;
  const bool auth = get16(p+6)==1;
  const uint16_t length=get16(p+4);
  if (n != kHeader+length+(auth ? kTag : 0)) return false;
  out={Type(p[3]),get64(p+8),get32(p+16),p+kHeader,length,auth};
  return true;
}
// 每次授权使用随机令牌；停车即销毁令牌，迟到控制不能重新起步。
struct Lease {
  uint64_t token=0;
  uint64_t request=0;
  uint32_t lastControl=0;
  bool locked=false;
  void stop(bool lock=false) { token=0; locked=locked || lock; }
  bool grant(uint64_t req, uint64_t value, uint32_t now) {
    if (locked || !req || !value || req==request) return false;
    request=req; token=value; lastControl=now; return true;
  }
  bool accepts(uint64_t value, uint32_t now) const {
    return token && token==value && !locked && !elapsed(now,lastControl,kControlTimeout);
  }
};
class Tilt {
 public:
  void reset() { *this=Tilt(); }
  bool sample(int16_t ax,int16_t ay,int16_t az,int32_t gx,int32_t gy,int32_t gz,uint32_t now) {
    lastRaw_=now; rawSeen_=true;
    const float norm=sqrtf(float(ax)*ax+float(ay)*ay+float(az)*az);
    if(norm<500 || norm>1600) return reject(InputReason::Acceleration);
    if(abs32(gx)>300000 || abs32(gy)>300000 || abs32(gz)>300000) return reject(InputReason::AngularSpeed);
    const bool captureSafe=norm>=850 && norm<=1150 && abs32(gx)<150000 && abs32(gy)<150000 && abs32(gz)<150000;
    recent_[next_]={ax,ay,az,now,captureSafe}; next_=uint8_t((next_+1)%4);
    if(count_<4) ++count_;
    const float dt=goodSeen_ && now-lastGood_<kInputTimeout ? float(now-lastGood_)/1000.0f : 0;
    if(!currentGood_ || !goodSeen_ || dt==0) { gravityX_=ax; gravityY_=ay; gravityZ_=az; }
    else {
      const float scale=0.000017453293f;
      const float wx=gx*scale,wy=gy*scale,wz=gz*scale;
      const float nx=gravityX_+dt*(wz*gravityY_-wy*gravityZ_);
      const float ny=gravityY_+dt*(wx*gravityZ_-wz*gravityX_);
      const float nz=gravityZ_+dt*(wy*gravityX_-wx*gravityY_);
      gravityX_=0.90f*nx+0.10f*ax; gravityY_=0.90f*ny+0.10f*ay; gravityZ_=0.90f*nz+0.10f*az;
    }
    // 不以普通大倾角撤销驾驶；仅在明显翻转时暂停，不偷偷更改参考点。
    if(calibrated_ && gravityX_*baseGX_+gravityY_*baseGY_+gravityZ_*baseGZ_<0)
      return reject(InputReason::Inverted);
    roll_=angle(gravityX_,gravityY_,gravityZ_); pitch_=angle(gravityY_,gravityX_,gravityZ_);
    currentGood_=true; goodSeen_=true; lastGood_=now;
    if(recovery_<2) ++recovery_;
    if(recovery_>=2) { lastUsable_=now; usableSeen_=true; }
    reason_=recovery_<2 ? InputReason::Settling : InputReason::None;
    return ready(now);
  }
  // 仅由控制任务在用户开始/取零命令中调用；不再进入页面就自动采桌面姿态。
  bool capture(uint32_t now) {
    if((!currentGood_ && reason_!=InputReason::Inverted) || rawAge(now)>=80) return false;
    const auto& latest=recent_[(next_+3)%4];
    float x=0,y=0,z=0; unsigned count=0;
    for(unsigned i=0;i<count_;++i) {
      const auto& s=recent_[i];
      if(s.safe && !elapsed(now,s.at,100) &&
          fabsf(angle(s.x,s.y,s.z)-angle(latest.x,latest.y,latest.z))<4 &&
          fabsf(angle(s.y,s.x,s.z)-angle(latest.y,latest.x,latest.z))<4) {
        x+=s.x; y+=s.y; z+=s.z; ++count;
      }
    }
    if(count<2) return false;
    x/=count; y/=count; z/=count;
    const float norm=sqrtf(x*x+y*y+z*z);
    if(norm<850 || norm>1150) return false;
    baseGX_=gravityX_=x; baseGY_=gravityY_=y; baseGZ_=gravityZ_=z;
    baseX_=roll_=angle(x,y,z); baseY_=pitch_=angle(y,x,z);
    calibrated_=currentGood_=goodSeen_=usableSeen_=true;
    lastGood_=lastUsable_=lastRaw_; recovery_=2; reason_=InputReason::None;
    return true;
  }
  bool calibrated() const { return calibrated_; }
  uint32_t rawAge(uint32_t now) const { return rawSeen_ ? now-lastRaw_ : UINT32_MAX; }
  uint32_t age(uint32_t now) const { return usableSeen_ ? now-lastUsable_ : UINT32_MAX; }
  InputReason reason(uint32_t now) const { return rawAge(now)>=80 ? InputReason::Missing : reason_; }
  bool ready(uint32_t now) const { return calibrated_ && currentGood_ && recovery_>=2 && age(now)<80; }
  bool expired(uint32_t now) const { return age(now)>=kInputTimeout; }
  bool neutral(uint32_t now) const { return ready(now) && fabsf(roll_-baseX_)<5 && fabsf(pitch_-baseY_)<5; }
  int16_t linear() const { return currentGood_ && recovery_>=2 ? curve(pitch_-baseY_) : 0; }
  int16_t angular() const { return currentGood_ && recovery_>=2 ? curve(roll_-baseX_) : 0; }
 private:
  struct Sample { int16_t x,y,z; uint32_t at; bool safe; };
  bool reject(InputReason reason) { currentGood_=false; recovery_=0; reason_=reason; return false; }
  static float angle(float a,float b,float c) { return atan2f(a,sqrtf(b*b+c*c))*57.29578f; }
  static int32_t abs32(int32_t x) { return x<0 ? -x : x; }
  static int16_t curve(float value) {
    float v=fabsf(value)-5; if(v<=0) return 0; if(v>23) v=23;
    v/=23; return int16_t((value<0 ? -1 : 1)*v*v*1000);
  }
  Sample recent_[4]{};
  uint8_t next_=0,count_=0,recovery_=0;
  float baseX_=0,baseY_=0,roll_=0,pitch_=0;
  float gravityX_=0,gravityY_=0,gravityZ_=1000,baseGX_=0,baseGY_=0,baseGZ_=1000;
  uint32_t lastGood_=0,lastRaw_=0,lastUsable_=0;
  bool calibrated_=false,currentGood_=false,goodSeen_=false,rawSeen_=false,usableSeen_=false;
  InputReason reason_=InputReason::Missing;
};
} // namespace petlink
