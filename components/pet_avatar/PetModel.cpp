#include "PetModel.h"

#include <limits.h>

namespace pet {
namespace {
constexpr uint32_t kMaxAge = 10U * 365U * kDay;
constexpr uint32_t kCooldown[] = {30, 30, 8, 20, 2, 2};
constexpr Milestone kMilestones[kMilestoneCount] = {
    {1, 1, "HELLO, LITTLE SEED"}, {3, 2, "FIRST SPROUT"},
    {7, 3, "OUR FIRST WEEK"}, {14, 5, "GROWING TOGETHER"},
    {30, 8, "A HOME WITH YOU"}, {60, 12, "BEST LITTLE FRIEND"},
    {90, 16, "SO MANY MEMORIES"}, {100, 20, "ONE HUNDRED DAYS"}};
constexpr const char* kEvents[] = {
    "A leaf waves hello!", "Found a tiny pebble.", "Your sprout wants a hug.",
    "A cloud looks like you.", "Made a little flower.", "Dreamed of a picnic.",
    "A butterfly said hi!", "Saved you a berry.", "Practicing a tiny hop.",
    "The plant grew a leaf.", "Found a cozy sunbeam.", "A silly sneeze. Achoo!",
    "Drew a heart for you.", "Listening to the rain.", "A nap beside the pot.",
    "Counting little stars.", "A leaf hat fits well!", "Time for a small dance.",
    "A shiny drop of dew.", "Waving with both paws.", "Tried to hug a flower.",
    "A very brave explorer.", "Hiding a happy smile.", "Sharing a quiet moment.",
    "A tiny wish for you.", "Collected a soft petal.", "Singing without a sound.",
    "Home smells like bread.", "A new friend at the sill.", "Happy you are here."};

uint32_t add(uint32_t value, uint32_t delta, uint32_t cap = kStatMax) {
  return value >= cap || delta >= cap - value ? cap : value + delta;
}
uint32_t subtract(uint32_t value, uint32_t delta, uint32_t floor = 0) {
  return value <= floor || delta >= value - floor ? floor : value - delta;
}
uint32_t nextRandom(State& s) {
  s.randomSeed = s.randomSeed * 1664525U + 1013904223U;
  return s.randomSeed;
}
Nature preference(const State& s) {
  if (s.explorer > s.cuddly && s.explorer >= s.clever) return Nature::Explorer;
  if (s.clever > s.cuddly) return Nature::Clever;
  return Nature::Cuddly;
}
Form formPreference(const State& s) {
  if (s.leafAffinity == 0 && s.dewAffinity == 0 && s.fruitAffinity == 0)
    return Form::Undecided;
  if (s.dewAffinity > s.leafAffinity && s.dewAffinity >= s.fruitAffinity)
    return Form::Dew;
  if (s.fruitAffinity > s.leafAffinity) return Form::Fruit;
  return Form::Leaf;
}
bool nightAt(uint32_t age, uint32_t offset) {
  const uint32_t hour = ((age % kDay + offset) % kDay) / 3600U;
  return hour >= 21U || hour < 7U;
}
void touchDay(State& s) {
  const uint32_t today = s.ageSeconds / kDay;
  if (s.lastActiveDay != today) {
    s.activeDays = add(s.activeDays, 1, 3650);
    s.lastActiveDay = today;
  }
}
void resolveCall(State& s, uint32_t bit, uint32_t value, uint32_t& minutes) {
  if (value == 0 || (s.callMask & bit) == 0) return;
  if ((s.missedCallMask & bit) == 0)
    s.timelyResponses = add(s.timelyResponses, 1, 1000000);
  s.callMask &= ~bit;
  s.missedCallMask &= ~bit;
  minutes = 0;
}
void resolveNeeds(State& s) {
  resolveCall(s, 1, s.hunger, s.foodCallMinutes);
  resolveCall(s, 2, s.joy, s.joyCallMinutes);
}
void callMinute(State& s, uint32_t bit, uint32_t value, uint32_t& minutes) {
  if (value > 0) return;
  if ((s.callMask & bit) == 0) {
    s.callMask |= bit;
    minutes = 0;
    return;
  }
  if ((s.missedCallMask & bit) != 0) return;
  minutes = add(minutes, 1, 15);
  if (minutes >= 15) {
    s.missedCallMask |= bit;
    s.careMisses = add(s.careMisses, 1, 1000000);
  }
}
void milestones(State& s) {
  const uint32_t unlocked = unlockedMilestones(s) & ~s.claimedMilestones;
  for (uint32_t i = 0; i < kMilestoneCount; ++i) {
    if ((unlocked & (1U << i)) != 0) {
      s.claimedMilestones |= 1U << i;
      s.coins = add(s.coins, 20, 9999);
    }
  }
}
bool reward(State& s, Action action, uint16_t score) {
  const uint32_t index = static_cast<uint32_t>(action);
  if (index > static_cast<uint32_t>(Action::Play) || s.dailyRewards >= 12)
    return false;
  const uint32_t bit = 1U << index;
  s.bond = add(s.bond, (s.dailyCareMask & bit) ? 12 : 45);
  s.coins = add(s.coins, 1 + (action == Action::Play ? score / 5U : 0), 9999);
  if (action == Action::Pet) {
    s.cuddly = add(s.cuddly, 2, 1000000);
    s.fruitAffinity = add(s.fruitAffinity, 1);
  }
  if (action == Action::Play) {
    s.explorer = add(s.explorer, 2, 1000000);
    s.clever = add(s.clever, score >= 12 ? 3 : 1, 1000000);
    s.dewAffinity = add(s.dewAffinity, 2);
  }
  ++s.dailyRewards;
  s.dailyCareMask |= bit;
  return true;
}
}  // namespace

void initializeProgress(State& s, bool legacy) {
  s.growthStage = static_cast<uint32_t>(Stage::Seed);
  s.stageStartAge = s.ageSeconds;
  s.lastOverfeedAge = UINT32_MAX;
  if (!legacy) return;
  Stage preserved = Stage::Seed;
  if (day(s) >= 100 && s.activeDays >= 20) preserved = Stage::Centennial;
  else if (day(s) >= 61) preserved = Stage::Companion;
  else if (day(s) >= 31) preserved = Stage::Adult;
  else if (day(s) >= 15) preserved = Stage::Teen;
  else if (s.ageSeconds >= 600) preserved = Stage::Baby;
  s.growthStage = static_cast<uint32_t>(preserved);
  if (preserved == Stage::Adult || preserved == Stage::Companion ||
      preserved == Stage::Centennial) {
    if (!s.adultNature) s.adultNature = static_cast<uint32_t>(preference(s));
    s.adultForm = static_cast<uint32_t>(Form::Leaf);
  }
  s.blanket = s.sleeping;
  s.lightOff = s.sleeping;
}

State newState(uint32_t seed) {
  State s{};
  s.hunger = 8200;
  s.joy = 8000;
  s.energy = 9000;
  s.hygiene = 9000;
  s.coins = 10;
  s.lastActiveDay = UINT32_MAX;
  s.randomSeed = seed ? seed : 0x59415941;
  s.eventId = nextRandom(s) % 30U;
  s.brightness = 65;
  s.clockOffsetSeconds = 8U * 3600U;
  for (uint32_t& t : s.lastActions) t = UINT32_MAX;
  initializeProgress(s);
  return s;
}

bool valid(const State& s) {
  if (s.ageSeconds > kMaxAge || s.remainderSeconds != s.ageSeconds % 60U ||
      s.hunger > kStatMax || s.joy > kStatMax || s.energy > kStatMax ||
      s.hygiene > kStatMax || s.bond > kStatMax || s.coins > 9999 ||
      s.sleeping > 1 || s.nursery > 1 || s.clockTrusted > 1 ||
      s.clockOffsetSeconds >= kDay || s.untrustedSeconds > kMaxAge ||
      (s.lastEpoch && (s.lastEpoch < 1704067200U || s.lastEpoch >= 4102444800U)) ||
      (s.clockTrusted && !s.lastEpoch) ||
      s.brightness < 10 || s.brightness > 100 || s.eventId >= 30 ||
      s.adultNature > static_cast<uint32_t>(Nature::Clever) ||
      (s.claimedMilestones & ~((1U << kMilestoneCount) - 1U)) != 0 ||
      s.activeDays > 3650 || s.dailyRewards > 12 || s.dailyCareMask > 63 ||
      s.eventDay > s.ageSeconds / kDay || s.eventSeen > 1 || s.bestScore > 30 ||
      (s.lastActiveDay != UINT32_MAX && s.lastActiveDay > s.ageSeconds / kDay) ||
      s.growthStage > static_cast<uint32_t>(Stage::Toddler) ||
      s.stageStartAge > s.ageSeconds || s.adultForm > 3 ||
      s.leafAffinity > kStatMax || s.dewAffinity > kStatMax || s.fruitAffinity > kStatMax ||
      s.careMisses > 1000000 || s.timelyResponses > 1000000 || s.callMask > 3 ||
      (s.missedCallMask & ~s.callMask) != 0 || s.foodCallMinutes > 15 || s.joyCallMinutes > 15 ||
      s.poopCount > 3 || s.wasteMinutes >= 240 || s.illness > 15 ||
      s.treatmentSteps > 2 || s.overfeedCount > 3 || s.blanket > 1 ||
      s.lightOff > 1 || s.unlitMinutes > 1440 || s.careStyle > 3 ||
      s.stageMissBaseline > s.careMisses || s.stageTimelyBaseline > s.timelyResponses ||
      (s.lastOverfeedAge != UINT32_MAX && s.lastOverfeedAge > s.ageSeconds))
    return false;
  for (uint32_t t : s.lastActions)
    if (t != UINT32_MAX && t > s.ageSeconds) return false;
  return true;
}

void advance(State& s, uint32_t seconds, bool careClock) {
  const uint32_t oldAge = s.ageSeconds;
  const uint32_t newAge = add(oldAge, seconds, kMaxAge);
  for (uint32_t minute = oldAge / 60U + 1U; minute <= newAge / 60U; ++minute) {
    const uint32_t at = minute * 60U;
    const bool resting = s.sleeping || nightAt(at, s.clockOffsetSeconds);
    if (s.nursery) {
      s.hunger = add(s.hunger, 8);
      s.joy = add(s.joy, 4);
      s.hygiene = add(s.hygiene, 8);
      s.energy = add(s.energy, 6);
      s.poopCount = 0;
      s.wasteMinutes = 0;
      s.callMask = s.missedCallMask = 0;
      s.foodCallMinutes = s.joyCallMinutes = 0;
    } else {
      s.hunger = subtract(s.hunger, resting ? 3 : 6);
      s.joy = subtract(s.joy, resting ? 1 : 3);
      s.hygiene = subtract(s.hygiene, s.poopCount ? 8 : 4, 1000);
      s.energy = resting ? add(s.energy, s.sleeping && s.blanket && s.lightOff ? 14 : 6)
                         : subtract(s.energy, 3, 1000);
      if (!resting && s.feedCount) {
        if (++s.wasteMinutes >= 240) {
          s.wasteMinutes = 0;
          s.poopCount = add(s.poopCount, 1, 3);
        }
      }
      if (s.poopCount >= 3 && s.hygiene < 3000) s.illness |= Illness::Dirty;
      if (s.energy <= 1000 && !resting) s.illness |= Illness::Exhausted;
      if (careClock && s.sleeping && (!s.blanket || !s.lightOff) &&
          s.unlitMinutes < 1440) {
        ++s.unlitMinutes;
        if (s.unlitMinutes % 30U == 0) s.joy = subtract(s.joy, 80);
      }
    }
    // 离线补算、托管和夜间不累计照顾失误，也不回补错过的互动奖励。
    if (careClock && !resting && !s.nursery && at >= 600) {
      callMinute(s, 1, s.hunger, s.foodCallMinutes);
      callMinute(s, 2, s.joy, s.joyCallMinutes);
    }
    if (at % kDay == 0) {
      s.dailyCareMask = 0;
      s.dailyRewards = 0;
      s.eventDay = at / kDay;
      s.eventId = nextRandom(s) % 30U;
      s.eventSeen = 0;
    }
  }
  s.ageSeconds = newAge;
  s.remainderSeconds = newAge % 60U;
}

ActionResult act(State& s, Action action, uint16_t score) {
  const uint32_t index = static_cast<uint32_t>(action);
  if (index >= static_cast<uint32_t>(Action::Count))
    return {false, false, Reaction::Idle, "NOT AVAILABLE"};
  const uint32_t last = s.lastActions[index];
  if (last != UINT32_MAX && s.ageSeconds - last < kCooldown[index])
    return {false, false, Reaction::Thinking, "ONE LITTLE MOMENT"};
  if (s.nursery && action != Action::Nursery)
    return {false, false, Reaction::Sleeping, "BACK FROM NURSERY?"};
  if ((s.sleeping || isNight(s)) && action == Action::Play)
    return {false, false, Reaction::Sleeping, "TIME TO REST"};
  if (action == Action::Feed && s.hunger >= 8500) {
    if (s.lastOverfeedAge != UINT32_MAX && s.ageSeconds - s.lastOverfeedAge < 30)
      return {false, false, Reaction::Thinking, "MY TUMMY IS FULL!"};
    s.lastOverfeedAge = s.ageSeconds;
    s.overfeedCount = add(s.overfeedCount, 1, 3);
    if (s.overfeedCount >= 3) s.illness |= Illness::Overfed;
    return {true, false, Reaction::Thinking, "FULL! NO MORE FOOD!"};
  }
  touchDay(s);
  s.lastActions[index] = s.ageSeconds;
  Reaction reaction = Reaction::Happy;
  const char* message = "SO HAPPY WITH YOU!";
  switch (action) {
    case Action::Feed:
      s.hunger = add(s.hunger, 2400);
      s.joy = add(s.joy, 250);
      s.feedCount = add(s.feedCount, 1, 1000000);
      s.overfeedCount = 0;
      reaction = Reaction::Eating;
      message = "NOM NOM. THANK YOU!";
      break;
    case Action::Clean:
      s.hygiene = add(s.hygiene, 3500);
      s.joy = add(s.joy, 350);
      s.cleanCount = add(s.cleanCount, 1, 1000000);
      reaction = Reaction::Washing;
      message = "SQUEAKY CLEAN!";
      break;
    case Action::Pet:
      s.joy = add(s.joy, 500);
      s.petCount = add(s.petCount, 1, 1000000);
      reaction = Reaction::Heart;
      message = "A TINY HUG FOR YOU";
      break;
    case Action::Play:
      if (score > 30) score = 30;
      s.joy = add(s.joy, 500 + score * 60);
      s.energy = subtract(s.energy, 200, 1000);
      s.playCount = add(s.playCount, 1, 1000000);
      if (score > s.bestScore) s.bestScore = score;
      message = "WE MAKE A GOOD TEAM!";
      break;
    case Action::Sleep:
      s.sleeping = !s.sleeping;
      s.blanket = 0;
      s.lightOff = 0;
      s.unlitMinutes = 0;
      reaction = s.sleeping ? Reaction::Sleeping : Reaction::Happy;
      message = s.sleeping ? "TUCK ME IN, PLEASE!" : "HELLO AGAIN!";
      break;
    case Action::Nursery:
      s.nursery = !s.nursery;
      message = s.nursery ? "SAFE AT NURSERY" : "WELCOME HOME!";
      break;
    case Action::Count: break;
  }
  const bool rewarded = reward(s, action, score);
  if (action == Action::Feed && rewarded) {
    if (score == static_cast<uint16_t>(Food::Dew)) s.dewAffinity = add(s.dewAffinity, 3);
    else if (score == static_cast<uint16_t>(Food::Fruit)) s.fruitAffinity = add(s.fruitAffinity, 3);
    else s.leafAffinity = add(s.leafAffinity, 3);
  }
  resolveNeeds(s);
  milestones(s);
  return {true, rewarded, reaction, message};
}

ActionResult interact(State& s, Form route, Nature personality) {
  const uint32_t cuddlyBefore = s.cuddly;
  const uint32_t fruitBefore = s.fruitAffinity;
  ActionResult result = act(s, Action::Pet);
  if (!result.rewarded) return result;
  if (personality != Nature::Cuddly) s.cuddly = cuddlyBefore;
  if (route != Form::Fruit) s.fruitAffinity = fruitBefore;
  if (route == Form::Leaf) s.leafAffinity = add(s.leafAffinity, 2);
  else if (route == Form::Dew) s.dewAffinity = add(s.dewAffinity, 2);
  else if (route == Form::Fruit) s.fruitAffinity = add(s.fruitAffinity, 2);
  if (personality == Nature::Clever) s.clever = add(s.clever, 2, 1000000);
  else if (personality == Nature::Explorer) s.explorer = add(s.explorer, 2, 1000000);
  return result;
}

ActionResult cleanWaste(State& s) {
  if (s.nursery || !s.poopCount) return {false, false, Reaction::Thinking, "ALL CLEAN ALREADY!"};
  --s.poopCount;
  s.hygiene = add(s.hygiene, 500);
  touchDay(s);
  return {true, false, Reaction::Washing, "ONE LESS LITTLE MESS!"};
}
ActionResult treat(State& s) {
  if (s.nursery || !s.illness) return {false, false, Reaction::Happy, "HEALTHY LITTLE FRIEND!"};
  if (s.treatmentSteps && (((s.illness & Illness::Dirty) &&
      (s.poopCount || s.hygiene < 4000)) ||
      ((s.illness & Illness::Overfed) && s.hunger > 8500) ||
      ((s.illness & Illness::Exhausted) && s.energy < 4000)))
    return {false, false, Reaction::Thinking, "CLEAN OR REST FIRST!"};
  ++s.treatmentSteps;
  touchDay(s);
  if (s.treatmentSteps < 2) return {true, false, Reaction::Heart, "MEDICINE 1/2. REST!"};
  s.illness = 0;
  s.treatmentSteps = 0;
  s.overfeedCount = 0;
  s.joy = add(s.joy, 500);
  resolveNeeds(s);
  return {true, false, Reaction::Happy, "HEALTHY AGAIN! 2/2"};
}
ActionResult tuckIn(State& s) {
  if (!s.sleeping || s.blanket) return {false, false, Reaction::Thinking, "COZY ENOUGH!"};
  s.blanket = 1;
  touchDay(s);
  return {true, false, Reaction::Sleeping, "A SOFT LITTLE BLANKET"};
}
ActionResult switchLight(State& s) {
  s.lightOff = !s.lightOff;
  if (s.sleeping) touchDay(s);
  return {true, false, s.sleeping ? Reaction::Sleeping : Reaction::Happy,
          s.lightOff ? "LIGHTS OUT. SWEET DREAMS!" : "A WARM LITTLE GLOW!"};
}

uint32_t day(const State& s) { return s.ageSeconds / kDay + 1U; }
Stage stage(const State& s) { return static_cast<Stage>(s.growthStage); }
GrowthProgress growthProgress(const State& s) {
  GrowthProgress p{};
  switch (stage(s)) {
    case Stage::Seed: p = {Stage::Baby, 600, 0, 0, 0, 0, false, false}; break;
    case Stage::Baby: p = {Stage::Toddler, 3 * kDay, 2, 2, 1, 1, false, false}; break;
    case Stage::Toddler: p = {Stage::Teen, 14 * kDay, 5, 5, 3, 3, false, false}; break;
    case Stage::Teen: p = {Stage::Adult, 30 * kDay, 8, 8, 5, 5, false, false}; break;
    case Stage::Adult: p = {Stage::Companion, 60 * kDay, 12, 12, 10, 8, false, false}; break;
    case Stage::Companion: p = {Stage::Centennial, 99 * kDay, 20, 20, 15, 12, false, false}; break;
    case Stage::Centennial: p.next = Stage::Centennial; p.complete = true; return p;
  }
  p.ready = s.ageSeconds >= p.ageRequired && s.activeDays >= p.daysRequired &&
            s.feedCount >= p.mealsRequired && s.playCount >= p.gamesRequired &&
            s.cleanCount >= p.bathsRequired;
  return p;
}
ActionResult evolve(State& s) {
  const GrowthProgress p = growthProgress(s);
  if (p.complete) return {false, false, Reaction::Heart, "OUR STORY CONTINUES!"};
  if (!p.ready) return {false, false, Reaction::Thinking, "SEE MY GROWTH GOALS!"};
  const uint32_t missedThisStage = s.careMisses - s.stageMissBaseline;
  s.careStyle = missedThisStage == 0 ? 1U : missedThisStage <= 2 ? 2U : 3U;
  s.stageMissBaseline = s.careMisses;
  s.stageTimelyBaseline = s.timelyResponses;
  s.growthStage = static_cast<uint32_t>(p.next);
  s.stageStartAge = s.ageSeconds;
  if (p.next == Stage::Adult) {
    s.adultNature = static_cast<uint32_t>(preference(s));
    const Form selected = formPreference(s);
    s.adultForm = static_cast<uint32_t>(selected == Form::Undecided ? Form::Leaf : selected);
  }
  return {true, false, Reaction::Heart, "LOOK! I AM GROWING!"};
}
Nature nature(const State& s) {
  return s.adultNature ? static_cast<Nature>(s.adultNature) : Nature::Undecided;
}
Form form(const State& s) {
  return s.adultForm ? static_cast<Form>(s.adultForm) : formPreference(s);
}
const char* formName(Form value) {
  switch (value) {
    case Form::Leaf: return "LEAF";
    case Form::Dew: return "DEW";
    case Form::Fruit: return "FRUIT";
    default: return "GROWING";
  }
}
const char* needText(const State& s) {
  if (s.nursery) return "SAFE AT NURSERY";
  if (s.illness) return "I NEED SOME CARE!";
  if (s.sleeping && !s.blanket) return "TUCK ME IN!";
  if (s.sleeping && !s.lightOff) return "TURN OFF THE LAMP!";
  if (s.callMask & 1U) return "MY TUMMY IS EMPTY!";
  if (s.callMask & 2U) return "PLAY WITH ME?";
  if (s.poopCount) return "A LITTLE MESS HERE!";
  if (growthProgress(s).ready) return "READY TO GROW!";
  return "";
}
uint8_t percent(uint32_t value) {
  return static_cast<uint8_t>((value > kStatMax ? kStatMax : value) / 100U);
}
bool isNight(const State& s) { return nightAt(s.ageSeconds, s.clockOffsetSeconds); }
uint32_t unlockedMilestones(const State& s) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < kMilestoneCount; ++i)
    if (day(s) >= kMilestones[i].day && s.activeDays >= kMilestones[i].activeDays)
      result |= 1U << i;
  return result;
}
const Milestone& milestone(uint32_t index) {
  return kMilestones[index < kMilestoneCount ? index : 0];
}
const char* eventText(const State& s) { return kEvents[s.eventId % 30U]; }
const char* greetingText(const State& s, uint32_t variation) {
  if (day(s) == 1) return "HELLO, NEW FRIEND!";
  if (day(s) >= 100 && s.activeDays >= 20) return "100 DAYS WITH YOU!";
  if (isNight(s)) return "SWEET DREAMS, FRIEND!";
  static constexpr const char* kGreetings[] = {
      "HI! I MISSED YOU!", "GOOD TO SEE YOU!", "LET'S HAVE FUN!",
      "YOUR LITTLE FRIEND IS HERE!", "A BIG HUG FOR YOU!", "READY TO EXPLORE?",
      "YOU MAKE ME HAPPY!", "LOOK WHAT I FOUND!", "A LOVELY DAY!",
      "CAN I FOLLOW YOU?", "YAY, YOU CAME BACK!", "TINY WAVE!"};
  return kGreetings[(variation + day(s) + s.eventId) % 12U];
}
const char* stageName(Stage value) {
  switch (value) {
    case Stage::Seed: return "LITTLE SEED";
    case Stage::Baby: return "BABY SPROUT";
    case Stage::Toddler: return "LITTLE SPROUT";
    case Stage::Teen: return "TEEN SPROUT";
    case Stage::Adult: return "GROWN SPROUT";
    case Stage::Companion: return "BEST FRIEND";
    case Stage::Centennial: return "100-DAY FRIEND";
  }
  return "SPROUT";
}
const char* natureName(Nature value) {
  switch (value) {
    case Nature::Undecided: return "STILL GROWING";
    case Nature::Explorer: return "EXPLORER";
    case Nature::Cuddly: return "CUDDLY";
    case Nature::Clever: return "CLEVER";
  }
  return "STILL GROWING";
}
}  // namespace pet
