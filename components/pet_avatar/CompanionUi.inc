void companionBubble(Canvas& c,const char* key,const char* speaker=nullptr) {
  char message[192],first[128],second[128];
  std::snprintf(message,sizeof(message),"%s%s",speaker ? translate(speaker,c.language) : "",translate(key,c.language));
  const char* rest=fitTextLine(message,first,sizeof(first),112);
  while(rest && *rest==' ') ++rest;
  fitTextLine(rest,second,sizeof(second),112);
  c.roundRect(3,80,122,26,5,kCream);
  text(c,8,82,first,kInk);
  if(second[0]) text(c,8,94,second,kInkSoft);
}
void companionChoice(Canvas& c,int x,int y,int w,int h,const char* key,uint16_t fill=kMintLight) {
  c.roundRect(x,y,w,h,5,fill);
  centeredText(c,x+w/2,y+(h-10)/2,key,kInk);
}
void renderCompanion(Canvas& c,const State& state,const View& view) {
  c.rect(0,0,128,128,kPaper);
  c.roundRect(2,1,124,18,5,kMintLight);
  text(c,7,6,"<",kInk); centeredText(c,64,5,"COMPANION",kInk); text(c,106,6,"...",kInk);
  if(view.companionMenu) {
    if(view.companionMenu==1) {
      companionChoice(c,8,27,112,23,"RECALIBRATE");
      companionChoice(c,8,56,112,24,"UNPAIR",kPinkLight);
      companionChoice(c,8,86,112,19,"BACK",kBlueLight);
    } else if(view.companionMenu==2) {
      centeredText(c,64,30,"UNPAIR PARTNER?",kInk);
      centeredText(c,64,48,"PET SAVE IS SAFE",kInkSoft);
      centeredText(c,64,62,"ONLINE: CLEAR BOTH",kInkSoft);
      companionChoice(c,5,81,56,24,"CANCEL",kBlueLight);
      companionChoice(c,67,81,56,24,"UNPAIR",kPinkLight);
    } else {
      const char* choices[]={"WAVE","RUN","HUG","SECRET","FIVE","PET"};
      for(int i=0;i<6;++i) companionChoice(c,4+(i%3)*41,29+(i/3)*39,38,32,choices[i],i%2 ? kBlueLight : kMintLight);
    }
    companionChoice(c,3,110,59,16,"BACK",kBlueLight);
    companionChoice(c,66,110,59,16,"STOP",kPinkLight);
    return;
  }
  Canvas stageCanvas(c.p+20*c.stride,128,59,c.stride);
  stageCanvas.language=c.language;
  drawCompanionWorld(stageCanvas,state,view.companionFrame);
  const char* status=nullptr;
  const bool performing=view.companionFrame.mode==CompanionMode::Welcome ||
    view.companionFrame.mode>=CompanionMode::Wave;
  if(view.linkMismatch) status="UPDATE BOTH DEVICES";
  else if(view.unpairState==1) status="UNPAIRING";
  else if(view.unpairState==2 || view.unpairState==4) status="PARTNER UNPAIRED";
  else if(view.unpairState==3) status="UNPAIRED HERE. CLEAR PEER TOO.";
  else if(view.unpairState==5) status="UNPAIR FAILED. TRY AGAIN.";
  else if(view.linkPhase==7) status="LINK RADIO ERROR";
  else if(view.linkConnected && view.linkFault && !performing) status="CAR NOT READY";
  else if(view.linkPhase==3) {
    char code[7]="000000"; uint32_t n=view.linkCode;
    for(int i=5;i>=0;--i) { code[i]=char('0'+n%10); n/=10; }
    c.roundRect(17,79,94,27,5,kBlueLight);
    centeredText(c,64,81,code,kInk); centeredText(c,64,94,"MATCH CODE?",kInkSoft);
  } else if(!view.linkConnected && !performing) status=view.linkHandshake>=2 ? "CHECKING BOTH WAYS" : view.linkBound ? "FINDING FRIEND" : "PAIRING AUTOMATICALLY";
  else if(view.driveZeroPending) status="SETTING CURRENT POSE";
  else if(view.drivePaused) status="INPUT PAUSED. KEEP STEADY.";
  else if(view.driveStarting) status="STARTING";
  else if(!view.driveRequested && !performing && view.linkReason==4) status="IMU LOST. TAP START.";
  else if(!view.driveCalibrated && !performing) status="TAP START TO SET ZERO";
  else if(!view.driveRequested && !performing && view.linkReason==9) status="FEEDBACK LOST. PARKED.";
  else if(!view.driveRequested && !performing && view.linkReason==2) status="SIGNAL WEAK. PARKED.";
  else if(!view.driveRequested && !performing && view.linkReason==6) status="PARKED. TAP START.";
  else if(!view.driveRequested && !performing && (view.linkReason==3 || view.linkReason==7)) status="BACK TOGETHER. TAP START.";
  if(status) companionBubble(c,status);
  else if(view.linkPhase!=3) {
    const bool rover=view.companionFrame.partner && view.companionFrame.speaker==0;
    companionBubble(c,companionLine(view.companionFrame,rover),rover ? "BAO:" : "SPROUT:");
  }
  const char* left=view.driveRequested ? "STOP" : view.linkPhase==3 ? (view.linkConfirmed ? "WAIT" : "FRIENDS") :
      view.linkBound ? "PLAY" : view.linkPhase==2 ? "FINDING" : "PAIR";
  companionChoice(c,3,110,59,17,left,view.driveRequested ? kPinkLight : kBlueLight);
  companionChoice(c,66,110,59,17,view.driveRequested ? "STOP" : "START",view.driveRequested ? kPinkLight : kMintLight);
}
