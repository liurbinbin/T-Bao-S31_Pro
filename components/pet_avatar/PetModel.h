#pragma once

#include <stdint.h>
#include "PetLocale.h"
#include "CompanionScene.h"

namespace pet {

constexpr uint32_t kDay = 86400;
constexpr uint32_t kStatMax = 10000;
constexpr uint32_t kMilestoneCount = 8;

enum class Stage : uint32_t { Seed, Baby, Teen, Adult, Companion, Centennial, Toddler };
enum class Food : uint32_t { Leaf, Dew, Fruit };
enum class Form : uint32_t { Undecided, Leaf, Dew, Fruit };
enum Illness : uint32_t { Overfed = 1, Toothache = 2, Dirty = 4, Exhausted = 8 };
enum class Nature : uint32_t { Undecided, Explorer, Cuddly, Clever };
enum class Action : uint32_t { Feed, Clean, Pet, Play, Sleep, Nursery, Count };
enum class Reaction : uint8_t {
  Idle,
  Happy,
  Eating,
  Washing,
  Heart,
  Sleeping,
  Thinking,
  Dizzy
};
enum class Page : uint8_t { Home, Care, Status, Memories, Settings, Game, Scene, Growth, CareLog, Companion };
enum class Scene : uint8_t { Room, Restaurant, Washroom, Park, Outdoor, Clinic };

struct State {
  uint32_t ageSeconds;
  uint32_t remainderSeconds;
  uint32_t hunger;
  uint32_t joy;
  uint32_t energy;
  uint32_t hygiene;
  uint32_t bond;
  uint32_t coins;
  uint32_t feedCount;
  uint32_t cleanCount;
  uint32_t petCount;
  uint32_t playCount;
  uint32_t explorer;
  uint32_t cuddly;
  uint32_t clever;
  uint32_t adultNature;
  uint32_t claimedMilestones;
  uint32_t activeDays;
  uint32_t lastActiveDay;
  uint32_t dailyCareMask;
  uint32_t dailyRewards;
  uint32_t eventDay;
  uint32_t eventId;
  uint32_t eventSeen;
  uint32_t randomSeed;
  uint32_t sleeping;
  uint32_t nursery;
  uint32_t bestScore;
  uint32_t clockTrusted;
  uint32_t lastEpoch;
  uint32_t clockOffsetSeconds;
  uint32_t untrustedSeconds;
  uint32_t brightness;
  uint32_t lastActions[static_cast<uint32_t>(Action::Count)];

  // 上方保持 V1 存档前缀不变；新增字段只允许追加并通过 SaveCodec 迁移。
  uint32_t growthStage;
  uint32_t stageStartAge;
  uint32_t leafAffinity;
  uint32_t dewAffinity;
  uint32_t fruitAffinity;
  uint32_t adultForm;
  uint32_t careMisses;
  uint32_t timelyResponses;
  uint32_t callMask;
  uint32_t missedCallMask;
  uint32_t foodCallMinutes;
  uint32_t joyCallMinutes;
  uint32_t poopCount;
  uint32_t wasteMinutes;
  uint32_t illness;
  uint32_t treatmentSteps;
  uint32_t overfeedCount;
  uint32_t lastOverfeedAge;
  uint32_t blanket;
  uint32_t lightOff;
  uint32_t unlitMinutes;
  uint32_t careStyle;
  uint32_t stageMissBaseline;
  uint32_t stageTimelyBaseline;
};

struct ActionResult {
  bool changed;
  bool rewarded;
  Reaction reaction;
  const char* message;
};

struct Milestone {
  uint16_t day;
  uint16_t activeDays;
  const char* title;
};

struct GrowthProgress {
  Stage next;
  uint32_t ageRequired;
  uint32_t daysRequired;
  uint32_t mealsRequired;
  uint32_t gamesRequired;
  uint32_t bathsRequired;
  bool ready;
  bool complete;
};

State newState(uint32_t seed);
void initializeProgress(State& state, bool legacy = false);
bool valid(const State& state);
void advance(State& state, uint32_t seconds, bool careClock = true);
GrowthProgress growthProgress(const State& state);
ActionResult evolve(State& state);
ActionResult cleanWaste(State& state);
ActionResult treat(State& state);
ActionResult tuckIn(State& state);
ActionResult switchLight(State& state);
ActionResult interact(State& state, Form route, Nature personality);
Form form(const State& state);
const char* formName(Form value);
const char* needText(const State& state);
ActionResult act(State& state, Action action, uint16_t score = 0);
Stage stage(const State& state);
Nature nature(const State& state);
uint32_t day(const State& state);
uint8_t percent(uint32_t value);
bool isNight(const State& state);
uint32_t unlockedMilestones(const State& state);
const Milestone& milestone(uint32_t index);
const char* eventText(const State& state);
const char* greetingText(const State& state, uint32_t variation);
const char* stageName(Stage value);
const char* natureName(Nature value);

struct View {
  Language language = Language::Chinese;
  Page page = Page::Home;
  Scene scene = Scene::Room;
  Reaction reaction = Reaction::Idle;
  uint32_t animationMs = 0;
  uint16_t batteryMv = 0;
  bool charging = false;
  bool clockKnown = false;
  bool saveError = false;
  const char* toast = nullptr;
  uint8_t memoryIndex = 0;
  uint8_t gameLane = 1;
  uint8_t dropLane = 1;
  int16_t dropY = 0;
  uint8_t gameScore = 0;
  uint8_t gameRemaining = 0;
  int16_t petX = 64;
  int16_t petY = 72;
  int16_t worldX = 80;
  int16_t cameraX = 0;
  int16_t petDepth = 50;
  int8_t petFacing = 1;
  uint8_t petScale = 100;
  bool imuReady = false;
  bool autoMoving = true;
  uint8_t sleepTransition = 0;
  uint8_t sleepProgress = 0;
  uint8_t secretEffect = 0;
  uint32_t dizzyUntilMs = 0;
  uint32_t sceneUntilMs = 0;
  uint8_t mealFood = 0;
  uint8_t mealBites = 0;
  uint8_t bathStep = 0;
  // 联动仅是临时视图，不进入养成 State 或 V2 存档。
  uint8_t linkPhase = 0;
  bool linkBound = false;
  bool linkConnected = false;
  bool linkFault = false;
  bool linkConfirmed = false;
  bool driveCalibrated = false;
  bool driveZeroPending=false,driveStarting=false,drivePaused=false;
  uint8_t linkHandshake=0;
  bool driveRequested = false;
  uint32_t linkCode = 0;
  uint16_t companionSeconds = 0;
  int16_t carBattery = -1;
  uint8_t linkReason=0,unpairState=0,companionMenu=0;
  bool linkMismatch=false;
  CompanionFrame companionFrame;
};

}  // namespace pet
