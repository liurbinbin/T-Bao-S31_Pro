// 由 PetLink.cpp 在 petlink namespace 内包含；密码实现与调度实现分离。
void Endpoint::disarm(bool lock,StopReason reason) {
  if(car_ && callbacks_.stop) callbacks_.stop();
  lease_.stop(lock); state_.linear=state_.angular=0;
  state_.inputPaused=false; firstControl_=false;
  pendingEvent_=0; state_.eventResult=2;
  if(car_) { if(!++sceneEpoch_) ++sceneEpoch_; }
  state_.reason=reason;
  if(!car_) needRelease_=true;
  if(connected_) state_.phase=Phase::Stopped;
}
void Endpoint::drive(bool enabled,bool fresh,bool neutral,int16_t linear,int16_t angular,bool paused) {
  held_=enabled; holdFresh_=fresh; holdNeutral_=neutral; inputPaused_=paused;
  inputLinear_=paused ? 0 : linear; inputAngular_=paused ? 0 : angular; holdUpdated_=clockMs();
}
void Endpoint::pet(uint8_t stage,uint8_t form,uint8_t nature) {
  if(stage!=petStage_ || form!=petForm_ || nature!=petNature_) petDirty_=true;
  petStage_=stage; petForm_=form; petNature_=nature;
}
Snapshot Endpoint::snapshot() {
  portENTER_CRITICAL(&mux_); Snapshot result=published_; portEXIT_CRITICAL(&mux_); return result;
}
void Endpoint::publish(uint32_t now) {
  if(car_ && callbacks_.info) {
    CarInfo info=callbacks_.info(); state_.battery=info.battery;
    state_.fault=info.fault || !info.available;
    if(state_.fault && lease_.token) disarm(true,StopReason::MotionFault);
  }
  state_.bound=bound_; state_.connected=connected_ && bound_; state_.active=active_;
  state_.confirmed=localConfirmed_; state_.locked=lease_.locked;
  state_.drivingMs=drivingMs_; state_.lastStatus=lastRx_; state_.sceneEpoch=sceneEpoch_;
  state_.ackAge=car_ && lease_.token ? now-lease_.lastControl : lastAckMs_ ? now-lastAckMs_ : 0;
  state_.businessAge=connected_ ? now-lastRx_ : UINT32_MAX;
  state_.controlAck=car_ ? streams_[0] : lastAckSeq_;
  state_.handshake=connected_ ? 5 : probing_ ? uint8_t(car_ ? readyAcked_ ? 4 : 2 : 3) : active_ ? 1 : 0;
  if(lease_.token) state_.phase=Phase::Driving;
  else if(active_ && connected_ && state_.phase!=Phase::Stopped) state_.phase=Phase::Ready;
  portENTER_CRITICAL(&mux_);
  state_.rxDropped=callbackDrops_+expiredRx_;
  state_.txFailed=txErrors_+callbackTxErrors_;
  published_=state_;
  portEXIT_CRITICAL(&mux_);
  previousPoll_=now;
}
bool Endpoint::sessionKey() {
  uint8_t context[24]{},material[32]{};
  put64(context+8,link_); put64(context+16,car_ ? boot_ : remoteBoot_);
  memcpy(context,"PR3-AUTH",8);
  bool ok=hmac(bond_.auth,context,sizeof(context),sessionKey_);
  memcpy(context,"PR3-LMK!",8);
  ok=ok && hmac(bond_.auth,context,sizeof(context),material) && peer(bond_.mac,material);
  mbedtls_platform_zeroize(material,sizeof(material));
  if(!ok) { mbedtls_platform_zeroize(sessionKey_,sizeof(sessionKey_)); state_.error=ESP_FAIL; }
  return ok;
}
void Endpoint::flushRx() {
  if(queue_) xQueueReset(queue_);
  portENTER_CRITICAL(&mux_); controlWaiting_=false; portEXIT_CRITICAL(&mux_);
}
void Endpoint::clearSession(uint32_t now,StopReason reason) {
  disarm(true,reason); connected_=probing_=readyAcked_=false;
  held_=wasHeld_=false; armRequest_=0; releasePending_=true; remoteBoot_=0;
  memset(streams_,0,sizeof(streams_)); memset(streamsSeen_,0,sizeof(streamsSeen_));
  pendingEvent_=0; state_.event=0; state_.phase=active_ ? Phase::Searching : Phase::Off;
  searchStarted_=now; searchInterval_=250; lastDiscover_=now-350;
  if(!car_) { link_=randomId(); sceneEpoch_=0; }
}
void Endpoint::established(uint32_t now) {
  connected_=true; probing_=false; lastRx_=now; ++state_.generation;
  petDirty_=releasePending_=true; state_.phase=Phase::Stopped;
  state_.error=ESP_OK; state_.mismatch=false; lastAckMs_=now; drivingMs_=0;
  state_.event=0; lastEvent_=0;
}
bool Endpoint::acceptSequence(Type type,uint32_t sequence) {
  unsigned stream=4;
  if(type==Type::Control || type==Type::Arm || type==Type::Stop || type==Type::Release || type==Type::Grant) stream=0;
  else if(type==Type::Status) stream=1;
  else if(type==Type::Heartbeat) stream=2;
  else if(type==Type::Event) stream=3;
  if(streamsSeen_[stream] && !newer(sequence,streams_[stream])) return false;
  streamsSeen_[stream]=true; streams_[stream]=sequence;
  return true;
}
bool Endpoint::eraseBond() {
  nvs_handle_t nvs;
  if(nvs_open("petlink",NVS_READWRITE,&nvs)!=ESP_OK) return false;
  const esp_err_t erased=nvs_erase_key(nvs,"bond");
  const bool ok=(erased==ESP_OK || erased==ESP_ERR_NVS_NOT_FOUND) && nvs_commit(nvs)==ESP_OK;
  nvs_close(nvs); return ok;
}
void Endpoint::finishForget(uint32_t now,UnpairState result) {
  if(!eraseBond()) {
    state_.unpair=UnpairState::Failed; state_.error=ESP_FAIL;
    forgetUntil_=0; return;
  }
  bound_=false; forgetUntil_=0; forgetGrace_=now+1800;
  state_.unpair=result; state_.phase=Phase::Stopped; state_.error=ESP_OK;
  // 暂留内存密钥与 peer，允许重复解绑请求重发确认；不再接受驾驶。
}
void Endpoint::requestForget(uint32_t now) {
  disarm(true,StopReason::Unpairing); endPair();
  held_=wasHeld_=false; armRequest_=0; pendingEvent_=0; pairAfterGrace_=false;
  if(!bound_) { state_.unpair=UnpairState::LocalOnly; return; }
  state_.unpair=UnpairState::Pending;
  forgetId_=randomId(); lastForgetTx_=now-150;
  if(connected_) forgetUntil_=now+1200;
  else finishForget(now,UnpairState::LocalOnly);
}
void Endpoint::interaction(uint8_t value,uint32_t now) {
  if(value<1 || value>6 || lease_.token || held_ || !connected_ || lease_.locked) {
    state_.eventResult=2; return;
  }
  if(car_ && callbacks_.event && !callbacks_.event(value)) { state_.eventResult=2; return; }
  pendingEvent_=++localEvent_; if(!pendingEvent_) pendingEvent_=++localEvent_;
  pendingEventValue_=value; pendingSeed_=uint32_t(randomId()); pendingStarted_=now+120; pendingEpoch_=sceneEpoch_;
  if(value==4) {
    static const uint8_t choices[3][4]={{1,5,6,14},{0,4,9,11},{2,7,10,15}};
    const uint8_t personality=car_ ? state_.nature : petNature_;
    const uint8_t line=personality>=1 && personality<=3 ? choices[personality-1][(pendingSeed_>>8)%4] : pendingSeed_%16;
    pendingSeed_=(pendingSeed_&~uint32_t(15))|line;
  }
  lastEventTx_=now-150;
  state_.event=value; state_.eventId=pendingEvent_; state_.eventOrigin=car_ ? 1 : 0;
  state_.eventSeed=pendingSeed_; state_.eventStarted=pendingStarted_;
  state_.eventResult=0; ++state_.eventRevision;
}
bool Endpoint::acceptInteraction(const Packet& p,uint32_t now) {
  if(p.length!=16 || p.data[4]<1 || p.data[4]>6 || p.data[5]!=(car_ ? 0 : 1)) return false;
  const uint32_t id=get32(p.data);
  const uint8_t origin=p.data[5];
  const int16_t age=int16_t(get16(p.data+10));
  if(!id || age< -200 || age>6500) return false;
  uint8_t ack[6]; put32(ack,id); ack[4]=origin; ack[5]=0;
  if(get32(p.data+12)!=sceneEpoch_) { send(Type::EventAck,ack,6); return false; }
  if(id==lastEvent_) { ack[5]=1; send(Type::EventAck,ack,6); return true; }
  if(!newer(id,lastEvent_) || lease_.token || held_ || lease_.locked) { send(Type::EventAck,ack,6); return false; }
  const bool simultaneous=state_.eventId && int32_t(now-state_.eventStarted)<800 &&
    state_.eventOrigin!=origin;
  // 同时点击时芽伴侧事件优先，两端使用相同规则，不因 ACK 顺序覆盖场景。
  if(simultaneous && state_.eventOrigin<origin) { send(Type::EventAck,ack,6); return false; }
  if(car_ && callbacks_.event && !callbacks_.event(p.data[4])) { send(Type::EventAck,ack,6); return false; }
  lastEvent_=id; pendingEvent_=0; ack[5]=1;
  state_.event=p.data[4]; state_.eventId=id; state_.eventOrigin=origin;
  state_.eventSeed=get32(p.data+6); state_.eventStarted=now-uint32_t(int32_t(age));
  state_.eventResult=1; ++state_.eventRevision;
  send(Type::EventAck,ack,6); return true;
}
void Endpoint::handle(const Rx& rx,uint32_t now) {
  Packet p{};
  if(!decode(rx.bytes,rx.size,p)) {
    // 旧固件提示只认已绑定 MAC 的有效认证发现，不因陌生广播误报。
    if(!connected_ && bound_ && rx.size>=kHeader+kTag && rx.bytes[0]=='P' && rx.bytes[1]=='R' &&
       (rx.bytes[2]==1 || rx.bytes[2]==2) && equal(rx.mac,bond_.mac,6) && get16(rx.bytes+6)==1 &&
       rx.size==kHeader+get16(rx.bytes+4)+kTag &&
       (rx.bytes[3]==uint8_t(Type::Discover) || rx.bytes[3]==uint8_t(Type::Welcome))) {
      uint8_t tag[32];
      if(hmac(bond_.auth,rx.bytes,rx.size-kTag,tag) && equal(tag,rx.bytes+rx.size-kTag,kTag))
        state_.mismatch=true;
    }
    return;
  }
  const uint32_t maxAge=(p.type==Type::Control || p.type==Type::Arm) ? kInputTimeout : 1200;
  if(elapsed(now,rx.received,maxAge)) { ++expiredRx_; return; }
  if(p.type==Type::Offer) {
    const bool before=crypto_ && crypto_->derived;
    if(active_ && pairing_ && !p.authenticated && derive(rx,p) && !before)
      send(Type::Offer,crypto_->offer,82,true,true);
    return;
  }
  if(!equal(rx.mac,bond_.mac,6) || !p.authenticated ||
     (!bound_ && !(crypto_ && crypto_->derived) && !forgetGrace_)) return;
  if(!active_ && p.type!=Type::Unbind && p.type!=Type::UnbindAck && p.type!=Type::Confirm) return;
  uint8_t tag[32];
  const uint8_t* key=rootMessage(p.type) ? bond_.auth : sessionKey_;
  if(!hmac(key,rx.bytes,kHeader+p.length,tag) || !equal(tag,rx.bytes+kHeader+p.length,kTag)) {
    ++state_.authFailed; return;
  }
  if(p.type==Type::Confirm) {
    if(p.length || !pairId_ || p.session!=pairId_) return;
    if(pairing_) remoteConfirmed_=true;
    else if(bound_ && int32_t(confirmUntil_-now)>0 && elapsed(now,lastConfirmTx_,150)) {
      send(Type::Confirm,nullptr,0); lastConfirmTx_=now;
    }
    return;
  }
  if(p.session==link_ && p.type==Type::Unbind && p.length==8 && get64(p.data)) {
    const uint64_t id=get64(p.data);
    if(bound_) {
      disarm(true,StopReason::Unpairing); forgetId_=id;
      finishForget(now,UnpairState::Peer);
    }
    uint8_t ack[9]; put64(ack,id);
    ack[8]=!bound_ && id==forgetId_ ? 1 : 0;
    send(Type::UnbindAck,ack,9); return;
  }
  if(p.session==link_ && p.type==Type::UnbindAck && p.length==9 && forgetUntil_ && get64(p.data)==forgetId_) {
    if(p.data[8]==1) finishForget(now,UnpairState::Both);
    else { state_.unpair=UnpairState::Failed; state_.error=ESP_FAIL; forgetUntil_=0; }
    return;
  }
  if(!forgetUntil_ && !forgetGrace_ && (connected_ || probing_) && p.type==Type::Goodbye && p.session==link_ && !p.length && acceptSequence(p.type,p.seq)) {
    clearSession(now,StopReason::LinkLost); return;
  }
  if(!bound_ || !active_ || pairing_ || forgetUntil_ || forgetGrace_) return;
  if(car_ && p.type==Type::Discover && !p.length && p.session) {
    if(p.session!=link_ || (!connected_ && !probing_)) {
      for(auto retired:retiredLinks_) if(retired && retired==p.session) return;
      if(link_ && (connected_ || probing_)) retiredLinks_[retiredLinkIndex_++%2]=link_;
      clearSession(now,StopReason::Reconnecting); link_=p.session; boot_=randomId(); lease_.request=0;
      if(!sessionKey()) return;
      probing_=true; probeStarted_=now; readyAcked_=false;
    }
    if(!acceptSequence(p.type,p.seq)) return;
    // 根认证广播不依赖退出前的旧 LMK；发现请求绝不刷新业务保活。
    uint8_t welcome[8]; put64(welcome,boot_); send(Type::Welcome,welcome,8,true); return;
  }
  if(p.session!=link_) return;
  if(!car_ && p.type==Type::Welcome && p.length==8) {
    const uint64_t boot=get64(p.data); if(!boot) return;
    if(remoteBoot_!=boot) {
      for(auto retired:retiredBoots_) if(retired && retired==boot) return;
      if(remoteBoot_) retiredBoots_[retiredBootIndex_++%2]=remoteBoot_;
      disarm(true,StopReason::Reconnecting); connected_=false; remoteBoot_=boot; sceneEpoch_=0;
      held_=wasHeld_=false; armRequest_=0;
      memset(streams_,0,sizeof(streams_)); memset(streamsSeen_,0,sizeof(streamsSeen_)); lastEvent_=0;
      if(!sessionKey()) { remoteBoot_=0; return; }
      probing_=true; probeStarted_=now; lastReadyTx_=now-150;
      petDirty_=releasePending_=true; state_.mismatch=false;
    }
    if(!acceptSequence(p.type,p.seq)) return;
    uint8_t primary=0; wifi_second_chan_t secondary;
    if(esp_wifi_get_channel(&primary,&secondary)==ESP_OK) lastChannel_=primary;
    return;
  }
  if(car_ && p.type==Type::LinkReady && p.length==8 && get64(p.data)==boot_ && (probing_ || connected_)) {
    if(acceptSequence(p.type,p.seq)) {
      readyAcked_=true; uint8_t ack[8]; put64(ack,boot_); send(Type::LinkAck,ack,8);
    }
    return;
  }
  if(!car_ && p.type==Type::LinkAck) {
    if(probing_ && p.length==8 && get64(p.data)==remoteBoot_ && acceptSequence(p.type,p.seq)) {
      established(now);
      const uint8_t profile[4]={0,petStage_,petForm_,petNature_};
      send(Type::Heartbeat,profile,4); lastHeartbeat_=now;
    }
    return;
  }
  const bool profile=(p.type==Type::Heartbeat || p.type==Type::Pet) && p.length==4 &&
    p.data[0]==(car_ ? 0 : 1) && p.data[1]<=6 && p.data[2]<=3 && p.data[3]<=3;
  const bool acceptedHeartbeat=car_ && probing_ && readyAcked_ && p.type==Type::Heartbeat && profile;
  if(acceptedHeartbeat) {
    if(!acceptSequence(p.type,p.seq)) return;
    established(now);
  }
  if(!connected_) return;
  const bool business=profile || (p.type==Type::Event && p.length==16) || (p.type==Type::EventAck && p.length==6) ||
    (car_ ? ((p.type==Type::Control && p.length==16) || (p.type==Type::Arm && p.length==20) ||
      (p.type==Type::Release && p.length==4) || (p.type==Type::Stop && !p.length)) :
      ((p.type==Type::Status && p.length==44) || (p.type==Type::Grant && p.length==32)));
  if(!business) return;
  if(!acceptedHeartbeat && p.type!=Type::EventAck && !acceptSequence(p.type,p.seq)) return;
  lastRx_=now; state_.acknowledged=p.seq;
  if(p.type==Type::Heartbeat || p.type==Type::Pet) {
    if(p.length==4 && p.data[0]==(car_ ? 0 : 1) && p.data[1]<=6 && p.data[2]<=3 && p.data[3]<=3) {
      state_.stage=p.data[1]; state_.form=p.data[2]; state_.nature=p.data[3];
    }
    return;
  }
  if(p.type==Type::Event) { acceptInteraction(p,now); return; }
  if(p.type==Type::EventAck && p.length==6 && p.data[4]==(car_ ? 1 : 0) && get32(p.data)==pendingEvent_) {
    pendingEvent_=0;
    if(state_.eventId==get32(p.data) && state_.eventOrigin==p.data[4]) state_.eventResult=p.data[5] ? 1 : 2;
    return;
  }
  if(car_) {
    if(p.type==Type::Release && p.length==4 && get32(p.data)==sceneEpoch_) {
      if(lease_.token) disarm(false,StopReason::User);
      lease_.locked=false;
    } else if(p.type==Type::Stop && !p.length) {
      disarm(true,StopReason::RemoteStop);
    } else if(p.type==Type::Arm && p.length==20 && get64(p.data)==boot_ &&
        get32(p.data+16)==sceneEpoch_ && !state_.fault) {
      const uint64_t request=get64(p.data+8);
      if(!lease_.token && !lease_.locked && request && request!=lease_.request &&
          callbacks_.arm && callbacks_.arm() && lease_.grant(request,randomId(),now)) {
        armRequest_=request; state_.reason=StopReason::None;
      }
      if(lease_.token && request==armRequest_) {
        uint8_t grant[32]; put64(grant,boot_); put64(grant+8,request);
        put64(grant+16,lease_.token); put32(grant+24,p.seq); put32(grant+28,sceneEpoch_);
        send(Type::Grant,grant,32);
      }
    } else if(p.type==Type::Control && p.length==16 && lease_.accepts(get64(p.data),now)) {
      const int16_t linear=int16_t(get16(p.data+8)),angular=int16_t(get16(p.data+10));
      if(p.data[12]!=1 || p.data[13]>1 || p.data[14] || p.data[15] ||
          (p.data[13] && (linear || angular)) || linear< -1000 || linear>1000 || angular< -1000 || angular>1000) {
        disarm(true,StopReason::MotionFault); return;
      }
      lease_.lastControl=rx.received;
      if(!callbacks_.control || !callbacks_.control(linear,angular)) { disarm(true,StopReason::MotionFault); return; }
      state_.linear=linear; state_.angular=angular; state_.inputPaused=p.data[13]==1;
    }
  } else if(p.type==Type::Grant && p.length==32 && get64(p.data)==remoteBoot_ &&
      get32(p.data+28)==sceneEpoch_ && get64(p.data+8)==armRequest_ && held_ &&
      !needRelease_ && holdFresh_ && !state_.fault && !lease_.locked) {
    const uint64_t token=get64(p.data+16);
    if(token && !lease_.token) {
      lease_.token=token; armFirstSeq_=get32(p.data+24); lastAckMs_=now;
      lastAckSeq_=armFirstSeq_; firstControl_=true; state_.reason=StopReason::None;
    } // 重复 Grant 不重置 ACK 进展，也不反复插入零速度帧。
  } else if(p.type==Type::Status && p.length==44 && get64(p.data)==remoteBoot_) {
    const uint64_t token=get64(p.data+8); const uint32_t ack=get32(p.data+16);
    state_.battery=p.data[24]<=100 ? p.data[24] : -1;
    const uint32_t epoch=get32(p.data+40);
    const bool newEpoch=epoch && (!sceneEpoch_ || newer(epoch,sceneEpoch_));
    // 新停车代次必须立即生效；同一代次的旧 ACK 则不能撤销新驾驶请求。
    const bool current=newEpoch || (!lease_.token && !armRequest_) || !armFirstSeq_ || ack==armFirstSeq_ || newer(ack,armFirstSeq_);
    if(current) {
      state_.fault=(p.data[25]&1)!=0;
      state_.inputPaused=(p.data[25]&4)!=0;
      if(newEpoch) {
        sceneEpoch_=epoch; pendingEvent_=0; state_.eventResult=2;
      }
      lease_.locked=(p.data[25]&2)!=0;
      if(lease_.locked) pendingEvent_=0;
      const auto peerReason=p.data[26]>0 && p.data[26]<=uint8_t(StopReason::AckTimeout) ? StopReason(p.data[26]) : StopReason::RemoteStop;
      if(lease_.token && (token!=lease_.token || lease_.locked || state_.fault)) {
        disarm(true,state_.fault ? StopReason::MotionFault : peerReason);
      } else if((newEpoch || lease_.locked) && (state_.reason==StopReason::None || state_.reason==StopReason::Reconnecting)) state_.reason=peerReason;
      if(lease_.locked) needRelease_=true;
      state_.linear=int16_t(get16(p.data+20)); state_.angular=int16_t(get16(p.data+22));
    }
    if(lease_.token && token==lease_.token && current && (ack==lastAckSeq_ || newer(ack,lastAckSeq_))) {
      if(ack!=lastAckSeq_) { lastAckSeq_=ack; lastAckMs_=now; }
      if(state_.linear || state_.angular) {
        const uint32_t dt=now-eventSent_;
        if(eventSent_ && dt<=400) drivingMs_+=dt;
        eventSent_=now;
      } else eventSent_=0;
    }
  }
}
void Endpoint::discovery(uint32_t now) {
  if((connected_ || probing_) && !pairing_) return;
  if(!elapsed(now,lastDiscover_,pairing_ ? 350 : searchInterval_)) return;
  lastDiscover_=now;
  if(!car_ && !(crypto_ && crypto_->derived)) {
    uint8_t target=channel_;
    if(lastChannel_ && !elapsed(now,searchStarted_,700)) target=lastChannel_;
    else {
      channel_=uint8_t(firstChannel_+(channel_-firstChannel_+1)%channelCount_);
      if(channel_==firstChannel_ && searchInterval_<600) searchInterval_+=100;
    }
    esp_wifi_set_channel(target,WIFI_SECOND_CHAN_NONE);
  }
  if(pairing_) send(Type::Offer,crypto_->offer,82,true,true);
  else if(bound_ && !car_) send(Type::Discover,nullptr,0,true);
}
void Endpoint::poll() {
  if(!ready_) return;
  uint32_t now=clockMs();
  if(active_ && previousPoll_ && now-previousPoll_>state_.pollMaxGap) state_.pollMaxGap=now-previousPoll_;
  const uint32_t cmd=__atomic_exchange_n(&commands_,0,__ATOMIC_ACQ_REL);
  if(cmd&2) {
    if(forgetUntil_) finishForget(now,UnpairState::LocalOnly);
    const bool notify=bound_ && (connected_ || probing_);
    if(notify) send(Type::Goodbye,nullptr,0);
    disarm(true,StopReason::User); endPair(); active_=connected_=probing_=readyAcked_=false;
    held_=wasHeld_=false; armRequest_=0;
    state_.phase=Phase::Off; pendingEvent_=0; pairAfterGrace_=false;
    closingUntil_=notify ? now+100 : 0; lastCloseTx_=now;
    __atomic_store_n(&eventCommand_,0,__ATOMIC_RELEASE); flushRx();
    if(!notify) resetMacCache();
  }
  if((cmd&1) && !(cmd&2)) {
    disarm(true,StopReason::Reconnecting); closingUntil_=0; flushRx();
    if(!car_) sceneEpoch_=0;
    active_=true; connected_=probing_=readyAcked_=false; held_=wasHeld_=false; needRelease_=true;
    if(!forgetGrace_) link_=randomId();
    remoteBoot_=0; armRequest_=0; lease_.locked=true;
    memset(streams_,0,sizeof(streams_)); memset(streamsSeen_,0,sizeof(streamsSeen_)); pendingEvent_=0; drivingMs_=0;
    state_.phase=Phase::Searching; state_.error=ESP_OK; state_.mismatch=false;
    state_.unpair=UnpairState::None; state_.reason=StopReason::Reconnecting;
    petDirty_=releasePending_=true; searchStarted_=now; searchInterval_=250; lastDiscover_=now-350;
    if(!bound_) {
      pairAfterGrace_=forgetGrace_!=0;
      if(!forgetGrace_ && !startPair(now)) { state_.phase=Phase::Error; state_.error=ESP_FAIL; }
    }
  }
  if((cmd&4) && active_ && !bound_) {
    if(forgetGrace_) pairAfterGrace_=true;
    else if(!startPair(now)) { state_.phase=Phase::Error; state_.error=ESP_FAIL; }
  }
  if((cmd&8) && pairing_ && crypto_ && crypto_->derived) localConfirmed_=true;
  if(cmd&16) {
    __atomic_store_n(&eventCommand_,0,__ATOMIC_RELEASE);
    if(!car_ && connected_) send(Type::Stop,nullptr,0);
    const auto reason=StopReason(__atomic_load_n(&stopCommandReason_,__ATOMIC_ACQUIRE));
    disarm(true,reason); armRequest_=0; releasePending_=true;
    if(!car_) wasHeld_=false;
  }
  if(cmd&32) requestForget(now);
  if(car_ && lease_.token && elapsed(now,lease_.lastControl,kControlTimeout)) disarm(true,StopReason::ControlTimeout);
  for(unsigned i=0;i<12 && xQueueReceive(queue_,&incoming_,0)==pdTRUE;++i) handle(incoming_,clockMs());
  bool pending=false;
  portENTER_CRITICAL(&mux_);
  if(controlWaiting_) { incoming_=latestControl_; controlWaiting_=false; pending=true; }
  portEXIT_CRITICAL(&mux_);
  if(pending) handle(incoming_,clockMs());
  now=clockMs();
  if(pairing_ && int32_t(now-pairUntil_)>=0) { endPair(); state_.phase=Phase::Searching; localConfirmed_=false; }
  if(pairing_ && crypto_ && crypto_->derived && localConfirmed_ && elapsed(now,lastConfirmTx_,180)) {
    send(Type::Confirm,nullptr,0); lastConfirmTx_=now;
    if(remoteConfirmed_) {
      if(storeBond()) {
        bound_=true; confirmUntil_=now+10000; endPair(); state_.phase=Phase::Searching;
        searchStarted_=now; lastDiscover_=now-350;
      } else { endPair(); state_.phase=Phase::Error; state_.error=ESP_FAIL; }
    }
  }
  if(forgetUntil_) {
    if(elapsed(now,lastForgetTx_,150)) { uint8_t id[8]; put64(id,forgetId_); send(Type::Unbind,id,8); lastForgetTx_=now; }
    if(int32_t(now-forgetUntil_)>=0) finishForget(now,UnpairState::LocalOnly);
  }
  if(forgetGrace_ && int32_t(now-forgetGrace_)>=0) {
    esp_now_del_peer(bond_.mac); mbedtls_platform_zeroize(&bond_,sizeof(bond_));
    mbedtls_platform_zeroize(sessionKey_,sizeof(sessionKey_));
    forgetGrace_=0; connected_=false; resetMacCache();
    if(active_ && pairAfterGrace_) {
      pairAfterGrace_=false;
      if(!startPair(now)) { state_.phase=Phase::Error; state_.error=ESP_FAIL; }
    }
  }
  if(closingUntil_) {
    if(int32_t(now-closingUntil_)>=0) { closingUntil_=0; resetMacCache(); }
    else if(elapsed(now,lastCloseTx_,40)) { send(Type::Goodbye,nullptr,0); lastCloseTx_=now; }
  }
  if(probing_ && !forgetUntil_ && !forgetGrace_ && elapsed(now,probeStarted_,1800)) {
    clearSession(now,StopReason::Reconnecting); ++state_.reconnects;
  }
  if(connected_ && !forgetUntil_ && !forgetGrace_ && elapsed(now,lastRx_,kLinkTimeout)) {
    clearSession(now,StopReason::LinkLost); ++state_.reconnects;
  }
  if(active_ && probing_ && !car_ && elapsed(now,lastReadyTx_,150)) {
    uint8_t ready[8]; put64(ready,remoteBoot_); send(Type::LinkReady,ready,8); lastReadyTx_=now;
  }
  if(active_ && !forgetUntil_ && !forgetGrace_) discovery(now);
  if(!active_ || !bound_ || !connected_ || forgetUntil_ || forgetGrace_) { publish(now); return; }
  if(!car_) {
    const bool fresh=holdFresh_ && !elapsed(now,holdUpdated_,kInputTimeout);
    if(!held_) {
      if(wasHeld_ || lease_.token || armRequest_) {
        send(Type::Stop,nullptr,0); disarm(true,StopReason::User); releasePending_=true;
      }
      needRelease_=false; armRequest_=0; lease_.token=0;
    } else if(!wasHeld_) {
      if(!fresh || !holdNeutral_ || needRelease_ || state_.fault || lease_.locked) needRelease_=true;
      else { armRequest_=randomId(); armFirstSeq_=0; lastControlTx_=now-40; state_.reason=StopReason::None; }
    }
    if(held_ && (!fresh || state_.fault || (lease_.token && elapsed(now,lastAckMs_,700)))) {
      if(lease_.token || armRequest_) send(Type::Stop,nullptr,0);
      disarm(true,!fresh ? StopReason::ImuStale : state_.fault ? StopReason::MotionFault : StopReason::AckTimeout);
      armRequest_=0;
    }
    wasHeld_=held_;
  }
  const uint8_t action=__atomic_exchange_n(&eventCommand_,0,__ATOMIC_ACQ_REL);
  if(action) interaction(action,now);
  if(car_ && elapsed(now,lastTx_,100)) {
    lastTx_=now;
    uint8_t status[44]{}; put64(status,boot_); put64(status+8,lease_.token); put32(status+16,streams_[0]);
    put16(status+20,uint16_t(state_.linear)); put16(status+22,uint16_t(state_.angular));
    status[24]=state_.battery<0 ? 255 : uint8_t(state_.battery);
    status[25]=(state_.fault ? 1 : 0)|(lease_.locked ? 2 : 0)|(state_.inputPaused ? 4 : 0);
    status[26]=uint8_t(state_.reason); status[27]=uint8_t(state_.unpair);
    put32(status+28,state_.generation); put32(status+32,state_.txFailed); put32(status+36,state_.rxDropped);
    put32(status+40,sceneEpoch_);
    send(Type::Status,status,44);
  }
  if(!car_ && elapsed(now,lastControlTx_,40)) {
    lastControlTx_=now;
    if(held_ && lease_.token && !needRelease_) {
      uint8_t control[16]{}; put64(control,lease_.token);
      const bool zero=firstControl_ || inputPaused_;
      put16(control+8,uint16_t(zero ? 0 : inputLinear_)); put16(control+10,uint16_t(zero ? 0 : inputAngular_));
      control[12]=1; control[13]=inputPaused_ ? 1 : 0;
      send(Type::Control,control,16); firstControl_=false;
    } else if(held_ && armRequest_ && !needRelease_) {
      uint8_t arm[20]; put64(arm,remoteBoot_); put64(arm+8,armRequest_); put32(arm+16,sceneEpoch_);
      send(Type::Arm,arm,20);
      if(!armFirstSeq_) armFirstSeq_=seq_;
    }
  }
  if(!car_ && !held_ && (releasePending_ || lease_.locked) && elapsed(now,lastRelease_,150)) {
    uint8_t release[4]; put32(release,sceneEpoch_);
    send(Type::Release,release,4); lastRelease_=now; releasePending_=false;
  }
  if(petDirty_) {
    const uint8_t profile[4]={uint8_t(car_ ? 1 : 0),petStage_,petForm_,petNature_};
    send(Type::Pet,profile,4); petDirty_=false; lastPet_=now;
  }
  if(elapsed(now,lastHeartbeat_,500)) {
    const uint8_t profile[4]={uint8_t(car_ ? 1 : 0),petStage_,petForm_,petNature_};
    send(Type::Heartbeat,profile,4); lastHeartbeat_=now;
  }
  if(pendingEvent_ && elapsed(now,lastEventTx_,150)) {
    const int32_t age=int32_t(now-pendingStarted_);
    if(age>6500) { pendingEvent_=0; state_.eventResult=2; }
    else {
      uint8_t event[16]; put32(event,pendingEvent_); event[4]=pendingEventValue_; event[5]=car_ ? 1 : 0;
      put32(event+6,pendingSeed_); put16(event+10,uint16_t(int16_t(age))); put32(event+12,pendingEpoch_);
      send(Type::Event,event,16); lastEventTx_=now;
    }
  }
  publish(now);
}
