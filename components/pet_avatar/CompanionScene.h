#pragma once
#include <stdint.h>

namespace pet {
enum class CompanionMode : uint8_t { Waiting, Idle, Welcome, Drive, Wave, Run, Hug, Secret, HighFive, Pet };
struct CompanionInput {
  bool car=false,connected=false,driving=false;
  uint32_t generation=0,eventRevision=0,eventId=0,eventSeed=0,eventStarted=0;
  uint8_t event=0,eventOrigin=0,eventResult=0,nature=0;
  int16_t linear=0,angular=0;
};
struct CompanionFrame {
  CompanionMode mode=CompanionMode::Waiting;
  bool car=false,partner=false;
  int16_t sproutX=36,roverX=92;
  int8_t sproutLift=0,roverLift=0,facing=1;
  uint8_t walk=0,eyes=0,effect=0,dialogue=0,speaker=0;
  uint32_t animationMs=0;
};
struct RobotDialogue { const char* sprout; const char* rover; };
inline const RobotDialogue& robotDialogue(uint8_t index) {
  // 稳定英文 key 由 PetLocale 翻译，两个角色分别拥有自己的语气。
  static const RobotDialogue lines[] = {
    {"THAT SIGNAL IS MY HUG.","BEEP. A SOFT SIGNAL."},
    {"I COUNT THEM IN LEAVES.","I COUNT CLOUDS IN BINARY."},
    {"IT MEANS: STAY CLOSE.","SECRET CODE: 0110."},
    {"ONLY A SPRINGTIME INSIDE.","NO SCREWS IN YOUR HEART?"},
    {"SAVE ONE FOR TOMORROW.","BATTERY OF HUGS: FULL."},
    {"LET'S KEEP IT IN A LEAF.","ANTENNA FOUND A STAR."},
    {"I WILL SHOW YOU THE PARK.","MY WHEELS DREAM OF GRASS."},
    {"MAYBE IT IS A BUTTERFLY.","A TINY BUG IN MY CODE."},
    {"I WAS HERE ALL ALONG.","RETRYING MY HELLO."},
    {"MY LEAF IS OUR UMBRELLA.","CAN I BORROW YOUR SHADE?"},
    {"SHH. THE FLOWERS LISTEN.","TICK TOCK. SECRET TIME."},
    {"YOU TURNED ON MY SMILE.","WHY DO YOU GLOW?"},
    {"WARM MY ROOTS WITH IT.","I STORED A LITTLE SUN."},
    {"NO MAP NEEDED, FRIEND.","ROUTE FOUND: TO YOU."},
    {"WE WILL GO TOGETHER.","BEEP. ONE MORE STEP?"},
    {"YES, IF THEY HAVE A SPROUT.","DO ROBOTS DREAM IN GREEN?"}
  };
  return lines[index%16];
}
inline const char* companionLine(const CompanionFrame& frame,bool rover) {
  if(frame.mode==CompanionMode::Secret) {
    const RobotDialogue& line=robotDialogue(frame.dialogue);
    return rover ? line.rover : line.sprout;
  }
  switch(frame.mode) {
    case CompanionMode::Waiting: return rover ? "MY ANTENNA IS WAITING." : "A FRIEND IS ON THE WAY.";
    case CompanionMode::Welcome: return rover ? "YOU FOUND MY FREQUENCY!" : "I BROUGHT A LITTLE SPRING!";
    case CompanionMode::Drive: return rover ? "STEADY WHEELS, SOFT HEART." : "LET'S FOLLOW THE BREEZE!";
    case CompanionMode::Wave: return rover ? "BEEP BEEP. HELLO!" : "MY LEAF WAVES TOO!";
    case CompanionMode::Run: return rover ? "TINY WHEELS, BIG DREAMS!" : "CATCH MY LITTLE LEAF!";
    case CompanionMode::Hug: return rover ? "WARMTH RECEIVED." : "A HUG NEEDS NO PASSWORD.";
    case CompanionMode::HighFive: return rover ? "HIGH FIVE. SYNC COMPLETE!" : "WE MAKE A LITTLE TEAM!";
    case CompanionMode::Pet: return rover ? "SOFT TOUCH DETECTED." : "THAT TICKLES MY SPROUT!";
    default: return rover ? "I SAVED A PLACE FOR YOU." : "IT IS COZY NEXT TO YOU.";
  }
}
class CompanionScene {
 public:
  CompanionFrame update(const CompanionInput& input,uint32_t now) {
    if(input.connected && (!connected_ || generation_!=input.generation)) {
      generation_=input.generation; welcome_=now+180;
    }
    connected_=input.connected;
    if(input.eventRevision && revision_!=input.eventRevision) {
      revision_=input.eventRevision; event_=input.event;
      seed_=input.eventSeed; started_=input.eventStarted; preview_=false;
    }
    CompanionFrame f;
    f.car=input.car; f.partner=input.connected; f.animationMs=now;
    f.sproutX=input.car ? 92 : 36; f.roverX=input.car ? 36 : 92;
    f.mode=input.connected ? CompanionMode::Idle : CompanionMode::Waiting;
    f.walk=uint8_t((now/130)%4); f.eyes=0;
    if(!input.connected) {
      if(input.car) { f.roverX=64+triangle(now/220,8)-4; f.sproutX=160; }
      else { f.sproutX=64+triangle(now/220,8)-4; f.roverX=160; }
    }
    const int32_t age=int32_t(now-started_);
    if(event_ && age>=0 && uint32_t(age)<duration(event_) && (preview_ || input.eventResult!=2)) {
      static const CompanionMode modes[]={CompanionMode::Idle,CompanionMode::Wave,CompanionMode::Run,
        CompanionMode::Hug,CompanionMode::Secret,CompanionMode::HighFive,CompanionMode::Pet};
      f.mode=modes[event_<=6 ? event_ : 0];
      f.dialogue=uint8_t(seed_%16);
      f.speaker=uint8_t((age/2400)%2); f.effect=event_==3 ? 1 : event_==5 ? 2 : 0;
      if(event_==2) {
        int walk=triangle(uint32_t(age)/36,76);
        f.sproutX=int16_t(18+walk); f.roverX=int16_t(3+walk);
        f.facing=((uint32_t(age)/36)%152)<76 ? 1 : -1;
        f.sproutLift=int8_t((age/100)%2); f.roverLift=int8_t((age/120)%2);
      } else if(event_==3 || event_==5) {
        const int approach=age<650 ? age*12/650 : age>1700 ? (2200-age)*12/500 : 12;
        const int delta=approach<0 ? 0 : approach>12 ? 12 : approach;
        f.sproutX+=input.car ? -delta : delta; f.roverX+=input.car ? delta : -delta;
        f.sproutLift=event_==5 ? int8_t(triangle(uint32_t(age)/60,3)) : 0;
      }
      f.eyes=event_==3 || event_==6 ? 2 : 1;
    }
    const int32_t entry=int32_t(now-welcome_);
    if(input.connected && welcome_ && entry>=0 && entry<900 && !input.driving) {
      f.mode=CompanionMode::Welcome;
      const int16_t x=int16_t(151-entry*59/900);
      const int8_t jump=int8_t(triangle(uint32_t(entry)/45,7));
      if(input.car) { f.sproutX=x; f.sproutLift=jump; }
      else { f.roverX=x; f.roverLift=jump; }
      f.effect=2; f.eyes=1;
    }
    if(input.driving && input.connected) {
      f.mode=CompanionMode::Drive;
      const int drift=input.angular/130;
      f.sproutX=int16_t((input.car ? 92 : 36)+drift);
      f.roverX=int16_t((input.car ? 36 : 92)+drift);
      f.facing=input.angular< -80 ? -1 : 1;
      f.walk=uint8_t((now/(input.linear || input.angular ? 90 : 400))%4);
      f.eyes=1;
    }
    if(f.mode==CompanionMode::Idle) {
      f.sproutX+=int16_t(triangle(now/420,4)-2);
      f.roverX+=int16_t(triangle(now/520,4)-2);
      f.roverLift=int8_t((now/900)%2);
    }
    return f;
  }
  void cancel() { event_=0; preview_=false; welcome_=0; }
  void preview(uint8_t kind,uint32_t now,uint32_t seed) { event_=kind; started_=now; seed_=seed; preview_=true; }
 private:
  static int triangle(uint32_t phase,int size) {
    const int x=int(phase%uint32_t(size*2)); return x<size ? x : size*2-x;
  }
  static uint32_t duration(uint8_t kind) { return kind==4 ? 5600 : kind==2 ? 4400 : kind==3 || kind==5 ? 2200 : 1800; }
  bool connected_=false,preview_=false;
  uint32_t generation_=0,welcome_=0,revision_=0,started_=0,seed_=0;
  uint8_t event_=0;
};
} // namespace pet
