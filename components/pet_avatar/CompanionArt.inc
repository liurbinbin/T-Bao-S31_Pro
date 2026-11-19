// 与 Canvas/drawPet 同一翻译单元；世界坐标 128×56，车端整数放大两倍。
void drawRover(Canvas& c,const CompanionFrame& frame) {
  const int x=frame.roverX;
  const int floor=50;
  const int base=floor-frame.roverLift;
  const int head=base-37;
  const uint16_t screen=rgb(25,34,37);
  c.roundRect(x-20,floor+2,40,3,2,rgb(218,204,170));
  // 方形底座、黄色条纹和两个小轮足，保留实物的工业小机器人轮廓。
  c.roundRect(x-16,base-12,32,13,3,kInk);
  c.roundRect(x-14,base-10,28,9,2,kInkSoft);
  c.rect(x-11,base-7,13,2,kGold);
  c.line(x+6,base-10,x+11,base-5,kGold);
  c.line(x+10,base-10,x+14,base-6,kGold);
  for(int side=-1;side<=1;side+=2) {
    const int wheel=x+side*11;
    c.disc(wheel,base+1,3,kInk);
    c.disc(wheel,base+1,1,kGold);
    if(frame.walk&1) c.line(wheel-2,base+1,wheel+2,base+1,kInkSoft);
    else c.line(wheel,base-1,wheel,base+3,kInkSoft);
  }
  c.rect(x-4,head+25,8,5,kInk);
  c.rect(x-2,head+25,3,4,kGold);
  // 摄像头小天线，不占用真实摄像头或人脸跟随资源。
  c.rect(x+6,head-8,3,10,kInkSoft);
  c.disc(x+7,head-9,4,kInk);
  c.disc(x+7,head-9,2,kInkSoft);
  c.pixel(x+8,head-10,kWhite);
  c.roundRect(x-18,head,36,27,5,kInkSoft);
  c.roundRect(x-17,head+1,34,25,4,kInk);
  c.roundRect(x-15,head+3,28,20,3,screen);
  c.line(x-11,head+3,x-3,head+3,kInkSoft);
  c.disc(x+17,head+14,4,kGold);
  c.disc(x+17,head+14,2,kInk);
  c.pixel(x+18,head+13,kGold);
  const bool blink=frame.animationMs%3300>3060;
  for(int side=-1;side<=1;side+=2) {
    const int eye=x+side*7;
    if(blink) c.rect(eye-2,head+13,5,1,kMintLight);
    else if(frame.eyes==2) heart(c,eye-3,head+8,kPink);
    else if(frame.eyes==1) {
      c.line(eye-3,head+13,eye,head+10,kMintLight);
      c.line(eye,head+10,eye+3,head+13,kMintLight);
    } else {
      c.roundRect(eye-2,head+8,5,8,2,kMintLight);
      c.rect(eye-1,head+9,1,2,kWhite);
    }
  }
  c.rect(x-2,head+18,5,1,kPinkLight);
  c.pixel(x-10,head+17,kPink);
  c.pixel(x+7,head+17,kPink);
  const int arm=(frame.mode==CompanionMode::Wave || frame.mode==CompanionMode::Welcome) ?
    int((frame.animationMs/140)%2)*3-7 : 0;
  c.line(x-16,base-8,x-21,base-8+arm,kInkSoft);
  c.disc(x-21,base-8+arm,2,kGold);
  c.line(x+16,base-8,x+20,base-5,kInkSoft);
}
void drawCompanionWorld(Canvas& c,const State& state,const CompanionFrame& frame) {
  c.rect(0,0,128,64,kPaper);
  c.disc(15,12,9,kCream); c.disc(109,14,12,kCream);
  c.roundRect(4,50,120,7,3,rgb(232,223,190));
  const int scroll=(frame.mode==CompanionMode::Drive || frame.mode==CompanionMode::Run) ? int((frame.animationMs/90)%16) : 0;
  for(int i=0;i<10;++i) {
    c.rect(i*16-scroll,53,6,1,kLeafLight);
    if(i%3==0) { c.rect(i*16-scroll+4,48,1,3,kLeaf); c.pixel(i*16-scroll+5,47,kGold); }
  }
  auto sprout=[&]() {
    View avatar;
    avatar.page=Page::Companion; avatar.scene=Scene::Park;
    avatar.petX=frame.sproutX; avatar.petScale=78;
    avatar.petY=int16_t(50-petFootOffset(stage(state))*78/100-frame.sproutLift);
    avatar.petFacing=(frame.mode==CompanionMode::Run || frame.mode==CompanionMode::Drive) ? frame.facing :
      frame.sproutX<frame.roverX ? 1 : -1;
    avatar.animationMs=frame.animationMs;
    avatar.reaction=frame.eyes==2 ? Reaction::Heart : frame.eyes==1 ? Reaction::Happy : Reaction::Thinking;
    avatar.secretEffect=frame.effect;
    drawPet(c,state,avatar);
  };
  // 本地角色最后绘制，拥抱时不会被来访者完全遮住。
  if(frame.car) { if(frame.partner) sprout(); drawRover(c,frame); }
  else { if(frame.partner) drawRover(c,frame); sprout(); }
  if(frame.effect==1) { heart(c,60,15,kPink); heart(c,68,20,kPinkLight); }
  if(frame.effect==2) { star(c,60,9,kGold); star(c,78,14,kLeafLight); }
  if(frame.mode==CompanionMode::Secret) {
    c.roundRect(49,4,31,11,4,kBlueLight);
    text(c,53,6,"0110",kInkSoft);
  }
}
