#pragma once
#include "PetLinkCore.h"
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace petlink {
enum class Phase : uint8_t { Off, Searching, Pairing, Confirm, Ready, Driving, Stopped, Error };
struct Snapshot {
  Phase phase=Phase::Off;
  bool bound=false, connected=false, active=false, confirmed=false, locked=false;
  uint32_t code=0, lastStatus=0, acknowledged=0, drivingMs=0;
  int16_t linear=0, angular=0, battery=-1;
  uint8_t stage=0, form=0, nature=0, event=0;
  bool fault=false, mismatch=false, inputPaused=false;
  uint8_t handshake=0;
  uint32_t businessAge=UINT32_MAX,authFailed=0,txNoMem=0,pollMaxGap=0,controlAck=0;
  esp_err_t lastTxError=ESP_OK;
  esp_err_t error=ESP_OK;
  StopReason reason=StopReason::None;
  UnpairState unpair=UnpairState::None;
  uint32_t generation=0,sceneEpoch=0,txFailed=0,rxDropped=0,reconnects=0,ackAge=0;
  uint32_t eventId=0, eventSeed=0, eventStarted=0, eventRevision=0;
  uint8_t eventOrigin=0, eventResult=0;
};
struct CarInfo { int16_t battery=-1; bool available=false, fault=false; };
struct Callbacks {
  bool (*arm)()=nullptr;
  bool (*control)(int16_t,int16_t)=nullptr;
  void (*stop)()=nullptr;
  CarInfo (*info)()=nullptr;
  bool (*event)(uint8_t)=nullptr;
};
class Endpoint {
 public:
  // 网络状态只由 poll() 的任务写，其他任务仅发原子命令和读取快照。
  esp_err_t begin(bool car,const Callbacks& callbacks=Callbacks());
  esp_err_t resumeRadio();
  bool radioIdle() const { return !active_ && !closingUntil_; }
  void poll();
  void open() { command(1); }
  void close() { command(2); }
  void pair() { command(4); }
  void confirm() { command(8); }
  void stop(StopReason reason=StopReason::User) {
    __atomic_store_n(&stopCommandReason_,uint8_t(reason),__ATOMIC_RELEASE); command(16);
  }
  void forget() { command(32); }
  void event(uint8_t value) { __atomic_store_n(&eventCommand_,value,__ATOMIC_RELEASE); }
  void drive(bool enabled,bool fresh,bool neutral,int16_t linear,int16_t angular,bool paused=false);
  void pet(uint8_t stage,uint8_t form,uint8_t nature);
  Snapshot snapshot();
 private:
  struct Rx { uint8_t mac[6]; uint16_t size; uint32_t received; uint8_t bytes[kMaxFrame]; };
  // 保持绑定 blob 的旧布局，升级通信协议不要求重新绑定。
  struct Bond { uint32_t version; uint8_t mac[6],lmk[16],auth[32]; };
  struct Crypto;
  static Endpoint* instance_;
  static void receive(const esp_now_recv_info_t*,const uint8_t*,int);
  static void sent(const esp_now_send_info_t*,esp_now_send_status_t);
  void command(uint32_t value) { __atomic_fetch_or(&commands_,value,__ATOMIC_RELEASE); }
  void handle(const Rx&,uint32_t);
  void send(Type,const uint8_t*,uint16_t,bool broadcast=false,bool bootstrap=false);
  bool peer(const uint8_t*,const uint8_t*);
  bool startPair(uint32_t);
  void endPair();
  bool derive(const Rx&,const Packet&);
  bool storeBond();
  void disarm(bool lock=false,StopReason reason=StopReason::User);
  void publish(uint32_t);
  void discovery(uint32_t);
  bool sessionKey();
  void clearSession(uint32_t now,StopReason reason);
  void established(uint32_t now);
  void flushRx();
  uint64_t randomId();
  bool eraseBond();
  void finishForget(uint32_t,UnpairState);
  void requestForget(uint32_t);
  void interaction(uint8_t,uint32_t);
  bool acceptInteraction(const Packet&,uint32_t);
  bool acceptSequence(Type,uint32_t);

  bool car_=false,active_=false,bound_=false,connected_=false,pairing_=false;
  bool localConfirmed_=false,remoteConfirmed_=false,needRelease_=true,wasHeld_=false;
  bool rxSeen_=false,ready_=false;
  uint8_t mac_[6]{},channel_=1,firstChannel_=1,channelCount_=11,lastChannel_=0;
  Bond bond_{};
  Crypto* crypto_=nullptr;
  Callbacks callbacks_;
  QueueHandle_t queue_=nullptr;
  StaticQueue_t* queueControl_=nullptr;
  uint8_t* queueStorage_=nullptr;
  Rx incoming_{},latestControl_{};
  bool controlWaiting_=false;
  uint32_t callbackDrops_=0,callbackTxErrors_=0,txErrors_=0,expiredRx_=0;
  uint8_t tx_[kMaxFrame]{},sessionKey_[32]{};
  Snapshot state_,published_;
  static portMUX_TYPE mux_;
  uint32_t commands_=0;
  uint8_t eventCommand_=0,stopCommandReason_=uint8_t(StopReason::User);
  uint64_t link_=0,boot_=0,remoteBoot_=0,pairId_=0,armRequest_=0,forgetId_=0;
  uint32_t seq_=0,rxSeq_=0,pairUntil_=0,lastTx_=0,lastDiscover_=0,lastRx_=0;
  uint32_t confirmUntil_=0,lastConfirmTx_=0,lastEvent_=0,pendingEvent_=0,eventSent_=0,localEvent_=0;
  uint32_t drivingMs_=0,previousPoll_=0,holdUpdated_=0;
  uint8_t pendingEventValue_=0,petStage_=0,petForm_=0,petNature_=0;
  bool held_=false,holdFresh_=false,holdNeutral_=false,inputPaused_=false,firstControl_=false;
  bool probing_=false,readyAcked_=false;
  uint32_t probeStarted_=0,lastReadyTx_=0,closingUntil_=0,lastCloseTx_=0;
  uint64_t retiredBoots_[2]{},retiredLinks_[2]{};
  uint8_t retiredBootIndex_=0,retiredLinkIndex_=0;
  int16_t inputLinear_=0,inputAngular_=0;
  Lease lease_;
  uint32_t streams_[5]{};
  bool streamsSeen_[5]{};
  uint32_t armFirstSeq_=0,lastAckSeq_=0,lastAckMs_=0,lastControlTx_=0,lastHeartbeat_=0;
  uint32_t lastRelease_=0,lastPet_=0,searchStarted_=0,searchInterval_=250;
  bool petDirty_=true,releasePending_=true,pairAfterGrace_=false;
  uint32_t forgetUntil_=0,forgetGrace_=0,lastForgetTx_=0;
  uint32_t pendingSeed_=0,pendingStarted_=0,lastEventTx_=0,sceneEpoch_=1,pendingEpoch_=0;
};
} // namespace petlink
