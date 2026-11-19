#include "app_petlink.h"
#include <new>
#include "PetLink.h"
#include "PetLinkDrive.h"
#include "app_face_follow.h"
#include "battery_monitor.h"
#include "motion_manager.h"
#include "actuator_control.h"
#include "wifi_provisioning.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

namespace {
petlink::Endpoint* endpoint=nullptr; // 大型对象本体放在 PSRAM。
TaskHandle_t worker=nullptr;
uint32_t commands=0;
uint8_t requestedEvent=0;
bool blocked=true,opened=false,desiredOpen=false,gated=false;
esp_err_t initError=ESP_OK;
int16_t cachedBattery=-1;
bool cachedProtection=false;
uint32_t lastBattery=0;
void request(uint32_t mask) { __atomic_fetch_or(&commands,mask,__ATOMIC_RELEASE); }
bool arm() {
    if(!opened || (__atomic_load_n(&commands,__ATOMIC_ACQUIRE)&(2|16)) ||
        motion_manager_ring_arm()!=ESP_OK) return false;
    __atomic_store_n(&blocked,false,__ATOMIC_RELEASE);
    return true;
}
void stop() {
    __atomic_store_n(&blocked,true,__ATOMIC_RELEASE);
    motion_manager_ring_stop();
}
bool control(int16_t linear,int16_t angular) {
    if(__atomic_load_n(&blocked,__ATOMIC_ACQUIRE) || !opened) return false;
    // ±1000 映射完整 ±1.0，不再设置联动专用速度上限、额外死区或差速限幅。
    motion_request_t r{};
    r.linear=float(linear)*petlink::kRingLinearScale;
    r.angular=float(angular)*petlink::kRingAngularScale;
    r.ttl_ms=petlink::kControlTimeout; r.ramp_ms=150;
    r.servo_pulse_us=ACTUATOR_SERVO_PULSE_CENTER_US;
    return motion_manager_submit(MOTION_SOURCE_RING,&r)==ESP_OK;
}
petlink::CarInfo info() {
    motion_state_t motion{}; motion_manager_get_state(&motion);
    const uint32_t now=uint32_t(esp_timer_get_time()/1000);
    if(now-lastBattery>=1000) {
        lastBattery=now; battery_monitor_state_t battery{};
        const bool fresh=battery_monitor_get_state(&battery)==ESP_OK && battery.valid && !battery.data_stale;
        cachedBattery=fresh && battery.battery_percent>=0 && battery.battery_percent<=100 ? battery.battery_percent : -1;
        if(fresh) cachedProtection=battery.low_voltage_warning || battery.shutdown_countdown_active;
    }
    return {cachedBattery,motion.available && gated && !cachedProtection,motion.faulted || cachedProtection};
}
bool event(uint8_t value) {
    if(!opened || value<1 || value>6 || (__atomic_load_n(&commands,__ATOMIC_ACQUIRE)&(2|16|32))) return false;
    // 跑跑、拥抱、悄悄话等只在屏幕演出，绝不自动驱动车轮。
    // 没有运动权限也能正常打招呼，只省略实体点头。
    if(value!=1 || !gated || cachedProtection) return true;
    motion_segment_t segments[3]{};
    motion_state_t current{}; motion_manager_get_state(&current);
    int center=current.current_servo_pulse_us;
    if(center<ACTUATOR_SERVO_PULSE_MIN_US || center>ACTUATOR_SERVO_PULSE_MAX_US) center=1500;
    for(int i=0;i<3;++i) {
        segments[i].servo_enabled=true;
        int pulse=center+(i==1 ? -80 : i==0 ? 80 : 0);
        if(pulse<ACTUATOR_SERVO_PULSE_MIN_US) pulse=ACTUATOR_SERVO_PULSE_MIN_US;
        if(pulse>ACTUATOR_SERVO_PULSE_MAX_US) pulse=ACTUATOR_SERVO_PULSE_MAX_US;
        segments[i].servo_pulse_us=pulse; segments[i].duration_ms=350; segments[i].ramp_ms=150;
    }
    const esp_err_t result=motion_manager_start_sequence(MOTION_SOURCE_RING,segments,3);
    if(result!=ESP_OK) ESP_LOGW("PETLINK","head wave skipped: %s",esp_err_to_name(result));
    return true; // 屏幕招手不因实体舵机不可用而失去反馈。
}
void run(void*) {
    auto initialize=[]() {
        esp_err_t result=wifi_prepare_petlink();
        if(result==ESP_OK) {
            petlink::Callbacks cb; cb.arm=arm; cb.control=control; cb.stop=stop; cb.info=info; cb.event=event;
            result=endpoint->begin(true,cb);
        }
        return result;
    };
    esp_err_t err=initialize();
    uint32_t retryAt=uint32_t(esp_timer_get_time()/1000)+3000;
    __atomic_store_n(&initError,err,__ATOMIC_RELEASE);
    if(err!=ESP_OK) ESP_LOGE("PETLINK","initialization failed: %s",esp_err_to_name(err));
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        const uint32_t now=uint32_t(esp_timer_get_time()/1000);
        if(err!=ESP_OK && __atomic_load_n(&desiredOpen,__ATOMIC_ACQUIRE) && int32_t(now-retryAt)>=0) {
            err=initialize(); retryAt=now+3000;
            __atomic_store_n(&initError,err,__ATOMIC_RELEASE);
            if(err==ESP_OK) request(1);
        }
        const uint32_t cmd=__atomic_exchange_n(&commands,0,__ATOMIC_ACQ_REL);
        if(cmd&2) {
            endpoint->close(); endpoint->poll(); opened=false;
            motion_manager_ring_session(false); gated=false;
            wifi_petlink_leave();
        }
        if((cmd&1) && __atomic_load_n(&desiredOpen,__ATOMIC_ACQUIRE) && err==ESP_OK) {
            // 后台跟随不允许在退出同行时凭旧心跳重新接管。
            app_face_follow_stop();
            gated=motion_manager_ring_session(true)==ESP_OK;
            const esp_err_t radio=wifi_petlink_enter();
            if(radio!=ESP_OK) {
                if(gated) { motion_manager_ring_session(false); gated=false; }
                wifi_petlink_leave(); err=radio; retryAt=now+3000;
            }
            __atomic_store_n(&initError,radio,__ATOMIC_RELEASE);
            if(radio==ESP_OK) { opened=true; endpoint->open(); }
            // 配对/像素互动不依赖电机可用，驾驶仍由 gate 和 arm 双重限制。
        }
        if(cmd&4) endpoint->pair();
        if(cmd&8) endpoint->confirm();
        if(cmd&16) endpoint->stop();
        if(cmd&32) endpoint->forget();
        const uint8_t action=__atomic_exchange_n(&requestedEvent,0,__ATOMIC_ACQ_REL);
        if(action && !(cmd&(2|16|32))) endpoint->event(action);
        endpoint->poll();
        static petlink::Phase lastPhase=petlink::Phase::Off;
        static petlink::StopReason lastReason=petlink::StopReason::None;
        static uint8_t lastHandshake=0;
        static bool lastPaused=false;
        const petlink::Snapshot state=endpoint->snapshot();
        if(state.phase!=lastPhase || state.reason!=lastReason || state.handshake!=lastHandshake || state.inputPaused!=lastPaused) {
            ESP_LOGI("PETLINK","phase=%u stop=%u hs=%u paused=%u reconnects=%lu TX=%lu noMem=%lu err=%ld RXdrop=%lu auth=%lu bus_age=%lu ack=%lu/%lums gap=%lu",
                unsigned(state.phase),unsigned(state.reason),unsigned(state.handshake),unsigned(state.inputPaused),
                static_cast<unsigned long>(state.reconnects),static_cast<unsigned long>(state.txFailed),static_cast<unsigned long>(state.txNoMem),
                static_cast<long>(state.lastTxError),static_cast<unsigned long>(state.rxDropped),static_cast<unsigned long>(state.authFailed),
                static_cast<unsigned long>(state.businessAge),static_cast<unsigned long>(state.controlAck),static_cast<unsigned long>(state.ackAge),
                static_cast<unsigned long>(state.pollMaxGap));
            lastPhase=state.phase; lastReason=state.reason; lastHandshake=state.handshake; lastPaused=state.inputPaused;
        }
        if(xTaskGetTickCount()-wake>pdMS_TO_TICKS(100)) wake=xTaskGetTickCount();
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    }
}
}
extern "C" esp_err_t app_petlink_open(void) {
    if(!worker) {
        void* memory=heap_caps_calloc(1,sizeof(petlink::Endpoint),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!memory) return ESP_ERR_NO_MEM;
        endpoint=new(memory) petlink::Endpoint();
        // 此任务执行 NVS/Flash 路径，栈必须留在内部 RAM；队列和上下文优先 PSRAM。
        if(xTaskCreatePinnedToCoreWithCaps(run,"petlink",6144,nullptr,4,&worker,
                tskNO_AFFINITY,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS) {
            endpoint->~Endpoint(); heap_caps_free(memory); endpoint=nullptr; return ESP_ERR_NO_MEM;
        }
        ESP_LOGI("PETLINK","context=%u bytes PSRAM; stack=6144 internal; free=%u largest=%u",
            unsigned(sizeof(petlink::Endpoint)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    }
    __atomic_store_n(&desiredOpen,true,__ATOMIC_RELEASE);
    request(1); return ESP_OK;
}
extern "C" void app_petlink_close(void) {
    __atomic_store_n(&desiredOpen,false,__ATOMIC_RELEASE);
    __atomic_store_n(&requestedEvent,0,__ATOMIC_RELEASE); request(2); stop();
}
extern "C" void app_petlink_pair(void) { request(4); }
extern "C" void app_petlink_confirm(void) { request(8); }
extern "C" void app_petlink_stop(void) {
    __atomic_store_n(&requestedEvent,0,__ATOMIC_RELEASE); request(16); stop();
}
extern "C" void app_petlink_forget(void) {
    __atomic_store_n(&requestedEvent,0,__ATOMIC_RELEASE); request(32|16); stop();
}
extern "C" void app_petlink_event(uint8_t event) {
    if(event>=1 && event<=6) __atomic_store_n(&requestedEvent,event,__ATOMIC_RELEASE);
}
extern "C" void app_petlink_get_state(app_petlink_state_t* out) {
    if(!out) return;
    *out={}; out->battery=-1;
    if(endpoint) {
        const petlink::Snapshot s=endpoint->snapshot();
        out->phase=uint8_t(s.phase); out->bound=s.bound; out->connected=s.connected;
        out->active=s.active; out->confirmed=s.confirmed; out->locked=s.locked; out->fault=s.fault;
        out->code=s.code; out->linear=s.linear; out->angular=s.angular; out->battery=s.battery;
        out->stage=s.stage; out->form=s.form; out->nature=s.nature; out->event=s.event; out->error=s.error;
        out->mismatch=s.mismatch; out->reason=uint8_t(s.reason); out->unpair=uint8_t(s.unpair);
        out->generation=s.generation; out->event_revision=s.eventRevision; out->event_id=s.eventId;
        out->event_seed=s.eventSeed; out->event_started=s.eventStarted; out->event_origin=s.eventOrigin;
        out->event_result=s.eventResult; out->tx_failed=s.txFailed; out->rx_dropped=s.rxDropped;
        out->reconnects=s.reconnects; out->ack_age=s.ackAge;
        out->input_paused=s.inputPaused; out->handshake=s.handshake;
        out->business_age=s.businessAge; out->auth_failed=s.authFailed; out->tx_nomem=s.txNoMem;
        out->poll_max_gap=s.pollMaxGap; out->last_tx_error=s.lastTxError; out->ack_seq=s.controlAck;
    }
    const esp_err_t error=__atomic_load_n(&initError,__ATOMIC_ACQUIRE);
    if(error!=ESP_OK) { out->error=error; out->phase=7; out->fault=true; }
}
