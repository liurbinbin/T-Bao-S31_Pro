#include "game_pet_companion.h"
#include "PixelRenderer.h"
#include "CompanionScene.h"
#include "PetLocale.h"

namespace {
pet::CompanionScene scene;
pet::CompanionFrame frame;
}
extern "C" void pet_companion_reset(void) { scene=pet::CompanionScene(); frame=pet::CompanionFrame(); }
extern "C" void pet_companion_cancel(void) { scene.cancel(); }
extern "C" void pet_companion_preview(uint8_t kind,uint32_t ms) { scene.preview(kind,ms,ms^0xB40A1137U); }
extern "C" const char* pet_companion_dialogue(bool rover,bool english) {
    return pet::translate(pet::companionLine(frame,rover),english ? pet::Language::English : pet::Language::Chinese);
}
extern "C" void pet_companion_avatar(uint16_t* pixels,const app_petlink_state_t* source,uint32_t ms) {
    if(!source) return;
    // 这是芽伴来访资料；本地角色始终是宝仔，不建立第二份养成存档。
    pet::State sprout{};
    sprout.growthStage=source->stage<=6 ? source->stage : 0;
    sprout.adultForm=source->form<=3 ? source->form : 0;
    sprout.adultNature=source->nature<=3 ? source->nature : 0;
    sprout.clockOffsetSeconds=8*3600;
    pet::CompanionInput input;
    input.car=true; input.connected=source->connected; input.driving=source->phase==5;
    input.generation=source->generation; input.event=source->event; input.eventRevision=source->event_revision;
    input.eventId=source->event_id; input.eventSeed=source->event_seed; input.eventStarted=source->event_started;
    input.eventOrigin=source->event_origin; input.eventResult=source->event_result; input.nature=source->nature;
    input.linear=source->linear; input.angular=source->angular;
    frame=scene.update(input,ms);
    if(!pixels) return;
    pet::PixelRenderer renderer;
    renderer.renderCompanionStage(sprout,frame,pixels,COMPANION_STAGE_WIDTH,COMPANION_STAGE_HEIGHT);
}
