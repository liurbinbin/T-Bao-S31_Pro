#include "game_pet_companion.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "app_language.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ui_page_style.h"
#include "motion_manager.h"
#include "CompanionTheme.h"
#include "src/misc/cache/instance/lv_image_cache.h"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_puhui_20_4);
static lv_obj_t *s_root,*s_phase,*s_line1,*s_line2,*s_image,*s_first,*s_popup,*s_diag;
static lv_image_dsc_t s_descriptor;
static uint16_t *s_pixels;
static uint32_t s_last_tick,s_pending_at,s_notice_until,s_diag_tick;
static uint8_t s_pending_kind;
static esp_err_t s_open_error;
static void (*s_back)(void);
static bool s_root_scrollable,s_root_chain_h,s_root_chain_v;
static int32_t s_root_scroll_x,s_root_scroll_y;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static uint32_t read_codepoint(const char **text) {
    const unsigned char *p=(const unsigned char*)*text;
    if(!*p) return 0;
    uint32_t value=*p++; unsigned extra=0;
    if(value>=0xf0) { value&=7; extra=3; }
    else if(value>=0xe0) { value&=15; extra=2; }
    else if(value>=0xc0) { value&=31; extra=1; }
    while(extra && (*p&0xc0)==0x80) { value=(value<<6)|(*p++&63); --extra; }
    *text=(const char*)p; return value;
}
static unsigned dialogue_page(const char *value,unsigned wanted,char out[192]) {
    const char *start=value,*p=value; unsigned page=0; int width=0; uint32_t previous=0;
    out[0]=0;
    while(*p) {
        const char *next=p; const uint32_t code=read_codepoint(&next);
        const int advance=lv_font_get_glyph_width(&font_puhui_16_4,code,0);
        int candidate=width+advance;
        if(p>start) candidate=width-lv_font_get_glyph_width(&font_puhui_16_4,previous,0)+
            lv_font_get_glyph_width(&font_puhui_16_4,previous,code)+advance;
        if(candidate>278 && p>start) {
            if(page==wanted) { size_t size=(size_t)(p-start); if(size>191) size=191; memcpy(out,start,size); out[size]=0; }
            ++page; start=p; candidate=advance;
        }
        width=candidate; previous=code; p=next;
    }
    if(page==wanted) { size_t size=(size_t)(p-start); if(size>191) size=191; memcpy(out,start,size); out[size]=0; }
    return page+1;
}
static void set_label_text(lv_obj_t *object,const char *value) {
    if(!object) return;
    char page[192];
    if(object==s_line1 || object==s_line2) {
        const unsigned pages=dialogue_page(value,0,page);
        if(pages>1) dialogue_page(value,(now_ms()/2400)%pages,page);
        value=page; // 固定坐标换句段，绝不创建横向或纵向滚动动画。
    }
    if(strcmp(lv_label_get_text(object),value)!=0) lv_label_set_text(object,value);
}
static void label_format(lv_obj_t *object,const char *format,...) {
    char buffer[192]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    set_label_text(object,buffer);
}
static void close_popup(void) {
    if(s_popup) lv_obj_delete(s_popup);
    s_popup=NULL; s_diag=NULL;
}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int width,int height,const char *text) {
    lv_obj_t *object=lv_label_create(parent);
    if(!object) return NULL;
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,width,height);
    lv_obj_set_style_text_font(object,&font_puhui_16_4,0);
    lv_obj_set_style_text_color(object,lv_color_hex(PET_THEME_INK),0);
    lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_text_letter_space(object,0,0);
    lv_obj_set_style_text_line_space(object,0,0);
    set_label_text(object,text);
    return object;
}
static lv_obj_t *button(lv_obj_t *parent,int x,int y,int width,int height,
                       const char *text,uint32_t fill,lv_event_cb_t callback,lv_event_code_t code,uintptr_t value) {
    lv_obj_t *object=lv_button_create(parent);
    if(!object) return NULL;
    lv_obj_set_pos(object,x,y); lv_obj_set_size(object,width,height);
    ui_page_style_button(object,fill,PET_THEME_GOLD,14);
    lv_obj_set_style_border_color(object,lv_color_hex(PET_THEME_LINE),0);
    lv_obj_add_event_cb(object,callback,code,(void*)value);
    lv_obj_t *caption=label(object,0,0,width-6,22,text);
    if(caption) { lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(caption); }
    return object;
}
static lv_obj_t *popup(const char *title) {
    close_popup();
    s_popup=lv_obj_create(s_root);
    lv_obj_set_pos(s_popup,12,48); lv_obj_set_size(s_popup,296,208);
    lv_obj_set_style_bg_color(s_popup,lv_color_hex(PET_THEME_WHITE),0);
    lv_obj_set_style_bg_opa(s_popup,LV_OPA_COVER,0);
    lv_obj_set_style_radius(s_popup,18,0); lv_obj_set_style_pad_all(s_popup,0,0);
    lv_obj_set_style_border_width(s_popup,2,0);
    lv_obj_set_style_border_color(s_popup,lv_color_hex(PET_THEME_LINE),0);
    lv_obj_clear_flag(s_popup,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *heading=label(s_popup,10,8,272,26,title);
    lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    return s_popup;
}
static void dismiss_cb(lv_event_t *event) { (void)event; close_popup(); }
static void back_cb(lv_event_t *event) {
    (void)event; s_pending_kind=0; pet_companion_cancel(); app_petlink_stop();
    if(s_back) s_back();
}
static void stop_cb(lv_event_t *event) {
    (void)event; s_pending_kind=0; pet_companion_cancel(); app_petlink_stop();
    motion_manager_stop_all(); // 车端大停止键始终可以终止所有执行器来源。
    s_notice_until=now_ms()+1800;
}
static void perform(uint8_t kind) {
    app_petlink_state_t state; app_petlink_get_state(&state);
    if(state.phase==5) {
        app_petlink_stop(); s_pending_kind=kind; s_pending_at=now_ms();
        set_label_text(s_line1,app_language_text("先停一下，再一起玩。","Let's park before we play."));
        return;
    }
    if(state.connected) {
        if(state.locked) { s_pending_kind=kind; s_pending_at=now_ms(); }
        else app_petlink_event(kind);
    } else pet_companion_preview(kind,now_ms());
}
static void interaction_cb(lv_event_t *event) {
    const uintptr_t value=(uintptr_t)lv_event_get_user_data(event);
    const bool primary=(value&0x80)!=0;
    const uint8_t kind=(uint8_t)(value&0x7f);
    app_petlink_state_t state; app_petlink_get_state(&state);
    if(primary && s_open_error!=ESP_OK) { s_open_error=app_petlink_open(); return; }
    if(primary && state.phase==3) { app_petlink_confirm(); return; }
    if(primary && !state.bound) { app_petlink_pair(); return; }
    close_popup(); perform(kind);
}
static void actor_cb(lv_event_t *event) {
    if(s_popup) return;
    (void)event;
    perform(6);
}
static void confirm_forget_cb(lv_event_t *event) {
    (void)event; s_pending_kind=0;
    app_petlink_forget(); close_popup();
    set_label_text(s_phase,app_language_text("正在解除伙伴关系…","Removing pairing..."));
}
static void forget_cb(lv_event_t *event) {
    (void)event;
    lv_obj_t *panel=popup(app_language_text("解除伙伴关系？","Remove this companion?"));
    label(panel,14,40,266,88,app_language_text(
        "会先停止驾驶，不删除养成和网络设置。\n在线时会通知对方一同解除。",
        "Driving will stop. Pet saves and Wi-Fi stay.\nThe connected peer will be notified."));
    button(panel,12,137,125,46,app_language_text("取消","Cancel"),PET_THEME_BLUE,dismiss_cb,LV_EVENT_CLICKED,0);
    button(panel,153,137,125,46,app_language_text("确认解除","Remove"),PET_THEME_PINK,confirm_forget_cb,LV_EVENT_CLICKED,0);
}
static void more_cb(lv_event_t *event) {
    (void)event;
    lv_obj_t *panel=popup(app_language_text("一起玩些什么？","A little time together"));
    const char *zh[]={"招招手","一起跑","抱抱","悄悄话","击个掌","摸摸"};
    const char *en[]={"Wave","Run","Hug","Secrets","High five","Pet"};
    for(unsigned i=0;i<6;++i)
        button(panel,10+(i%3)*94,42+(i/3)*53,88,44,app_language_text(zh[i],en[i]),
               i%2 ? PET_THEME_BLUE : PET_THEME_MINT,interaction_cb,LV_EVENT_CLICKED,i+1);
    button(panel,12,156,266,36,app_language_text("返回小窝","Back"),PET_THEME_PAPER,dismiss_cb,LV_EVENT_CLICKED,0);
}
static void diagnostic_cb(lv_event_t *event) {
    (void)event;
    lv_obj_t *panel=popup(app_language_text("伙伴连接详情","Connection details"));
    s_diag=label(panel,14,40,266,112,""); s_diag_tick=now_ms()-500;
    button(panel,12,156,266,36,app_language_text("知道啦","Got it"),PET_THEME_BLUE,dismiss_cb,LV_EVENT_CLICKED,0);
}
static void menu_cb(lv_event_t *event) {
    (void)event;
    if(s_popup) { close_popup(); return; }
    lv_obj_t *panel=popup(app_language_text("伙伴小窝","Companion nook"));
    button(panel,14,42,264,43,app_language_text("更多互动","More play"),PET_THEME_MINT,more_cb,LV_EVENT_CLICKED,0);
    button(panel,14,94,264,43,app_language_text("连接详情","Connection details"),PET_THEME_BLUE,diagnostic_cb,LV_EVENT_CLICKED,0);
    button(panel,14,146,264,43,app_language_text("解除绑定","Remove pairing"),PET_THEME_PINK,forget_cb,LV_EVENT_CLICKED,0);
}
void game_pet_companion_build(lv_obj_t *root,void (*back)(void)) {
    s_root=root; s_back=back; s_last_tick=0; s_pending_kind=0; s_notice_until=0;
    s_root_scrollable=lv_obj_has_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    s_root_chain_h=lv_obj_has_flag(root,LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    s_root_chain_v=lv_obj_has_flag(root,LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    s_root_scroll_x=lv_obj_get_scroll_x(root); s_root_scroll_y=lv_obj_get_scroll_y(root);
    lv_obj_scroll_to(root,0,0,LV_ANIM_OFF);
    pet_companion_reset();
    ui_page_style_root(root);
    lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_color(root,lv_color_hex(PET_THEME_PAPER),0);
    lv_obj_set_style_text_color(root,lv_color_hex(PET_THEME_INK),0);
    button(root,6,5,42,34,"<",PET_THEME_MINT,back_cb,LV_EVENT_CLICKED,0);
    lv_obj_t *title=label(root,58,8,204,28,app_language_text("芽伴同行","Little companions"));
    lv_obj_set_style_text_font(title,&font_puhui_20_4,0);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    button(root,272,5,42,34,"...",PET_THEME_BLUE,menu_cb,LV_EVENT_CLICKED,0);
    s_phase=label(root,14,40,292,20,app_language_text("宝仔正在整理小窝…","Bao is getting the nook ready..."));
    lv_obj_set_style_text_color(s_phase,lv_color_hex(PET_THEME_MUTED),0);
    lv_label_set_long_mode(s_phase,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_phase,LV_TEXT_ALIGN_CENTER,0);
    // 一块 256×112 RGB565 舞台（57344 bytes），只分配 PSRAM，绝不复制全屏。
    s_pixels=heap_caps_calloc(COMPANION_STAGE_WIDTH*COMPANION_STAGE_HEIGHT,sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(s_pixels) {
        s_descriptor=(lv_image_dsc_t){0};
        s_descriptor.header.magic=LV_IMAGE_HEADER_MAGIC; s_descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
        s_descriptor.header.w=COMPANION_STAGE_WIDTH; s_descriptor.header.h=COMPANION_STAGE_HEIGHT;
        s_descriptor.header.stride=COMPANION_STAGE_WIDTH*2;
        s_descriptor.data_size=COMPANION_STAGE_WIDTH*COMPANION_STAGE_HEIGHT*2;
        s_descriptor.data=(const uint8_t*)s_pixels;
        s_image=lv_image_create(root); lv_image_set_src(s_image,&s_descriptor);
        lv_obj_set_pos(s_image,32,62); lv_obj_add_flag(s_image,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_image,actor_cb,LV_EVENT_CLICKED,NULL);
    } else label(root,28,100,264,48,app_language_text("小窝内存不足，仍可以连接和聊天。","Not enough picture memory; connection and chat still work."));
    lv_obj_t *bubble=lv_obj_create(root);
    lv_obj_set_pos(bubble,14,174); lv_obj_set_size(bubble,292,42);
    lv_obj_set_style_bg_color(bubble,lv_color_hex(PET_THEME_WHITE),0);
    lv_obj_set_style_radius(bubble,10,0); lv_obj_set_style_border_width(bubble,0,0);
    lv_obj_set_style_pad_all(bubble,0,0); lv_obj_clear_flag(bubble,LV_OBJ_FLAG_SCROLLABLE);
    const int line_height=lv_font_get_line_height(&font_puhui_16_4);
    s_line1=label(bubble,7,1,278,line_height,""); s_line2=label(bubble,7,1+line_height,278,line_height,"");
    lv_label_set_long_mode(s_line1,LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(s_line2,LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_line2,lv_color_hex(PET_THEME_MUTED),0);
    s_first=button(root,14,218,92,44,app_language_text("找伙伴","Find"),PET_THEME_MINT,interaction_cb,LV_EVENT_CLICKED,0x81);
    button(root,114,218,92,44,app_language_text("一起跑","Run"),PET_THEME_GOLD,interaction_cb,LV_EVENT_CLICKED,2);
    button(root,214,218,92,44,app_language_text("悄悄话","Secrets"),PET_THEME_BLUE,interaction_cb,LV_EVENT_CLICKED,4);
    lv_obj_t *stop=button(root,14,266,292,46,app_language_text("停止驾驶","STOP DRIVING"),PET_THEME_PINK,stop_cb,LV_EVENT_PRESSED,0);
    if(stop) lv_obj_set_style_text_color(lv_obj_get_child(stop,0),lv_color_hex(PET_THEME_STOP),0);
    s_open_error=app_petlink_open();
}
void game_pet_companion_exit(void) {
    s_pending_kind=0; app_petlink_close(); close_popup();
    if(s_image) lv_image_set_src(s_image,NULL);
    if(s_pixels) { lv_image_cache_drop(&s_descriptor); heap_caps_free(s_pixels); s_pixels=NULL; }
    if(s_root) {
        lv_obj_set_style_bg_color(s_root,lv_color_black(),0);
        lv_obj_set_style_text_color(s_root,lv_color_white(),0);
        if(s_root_scrollable) lv_obj_add_flag(s_root,LV_OBJ_FLAG_SCROLLABLE);
        if(s_root_chain_h) lv_obj_add_flag(s_root,LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
        if(s_root_chain_v) lv_obj_add_flag(s_root,LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_scroll_to(s_root,s_root_scroll_x,s_root_scroll_y,LV_ANIM_OFF);
    }
    s_root=s_phase=s_line1=s_line2=s_image=s_first=NULL; s_back=NULL;
}
void game_pet_companion_tick(uint32_t ms) {
    if(!s_phase || ms-s_last_tick<66) return;
    s_last_tick=ms;
    app_petlink_state_t state; app_petlink_get_state(&state);
    if(s_open_error!=ESP_OK) state.error=s_open_error;
    if(s_pending_kind) {
        if(state.connected && state.phase!=5 && !state.locked) { app_petlink_event(s_pending_kind); s_pending_kind=0; }
        else if(ms-s_pending_at>3000) { s_pending_kind=0; s_notice_until=ms+1800; }
    }
    pet_companion_avatar(s_popup ? NULL : s_pixels,&state,ms);
    if(s_pixels && !s_popup) { lv_image_cache_drop(&s_descriptor); lv_obj_invalidate(s_image); }
    const char *phase;
    if(state.unpair==1) phase=app_language_text("正在解除伙伴关系…","Removing pairing...");
    else if(state.unpair==2 || state.unpair==4) phase=app_language_text("双方已解除绑定","Pairing removed on both devices");
    else if(state.unpair==3) phase=app_language_text("本机已解绑，对方未确认","Removed here; peer did not confirm");
    else if(state.unpair==5) phase=app_language_text("保存失败，可再点一次解绑","Storage failed; you can retry");
    else if(state.mismatch) phase=app_language_text("请同步更新指环和小车固件","Update both firmwares together");
    else if(state.error!=ESP_OK) phase=app_language_text("连接模块忙，稍后自动再试","Connection busy; retrying shortly");
    else if(state.phase==3) phase=app_language_text("找到你啦，成为伙伴吧","There you are! Be friends?");
    else if(state.phase==2) phase=app_language_text("正在寻找新伙伴…","Looking for a new friend...");
    else if(!state.connected && state.handshake>=2) phase=app_language_text("正在校验双向连接…","Checking both directions...");
    else if(!state.connected) phase=state.bound ? app_language_text("自动找回伙伴中…","Finding our way back...") : app_language_text("点找伙伴，再次发出邀请","Tap Find friend to invite again");
    else if(state.input_paused) phase=app_language_text("输入暂缓，轮子目标已归零","Input paused; zero target");
    else if(state.phase==5) phase=app_language_text("体感驾驶中 · 随倾斜调速","Driving; tilt adjusts power");
    else if(state.fault) phase=app_language_text("驾驶暂不可用，仍可以聊天","Driving unavailable; we can still chat");
    else if(state.reason==2) phase=app_language_text("信号暂弱，已停车；指环可重新开始","Signal weak; parked. Tap Start on ring.");
    else phase=app_language_text("伙伴在身边 · 已停止驱动","Together, with wheels stopped");
    set_label_text(s_phase,phase);
    if(state.phase==3) {
        label_format(s_line1,app_language_is_english() ? "Secret code: %06lu" : "小暗号：%06lu",(unsigned long)state.code);
        set_label_text(s_line2,app_language_text("两边各点一次「成为伙伴」即可。","Tap Be friends once on each device."));
    } else if(s_pending_kind) {
        set_label_text(s_line1,app_language_text("宝仔：先停一下，再一起玩。","Bao: Let's park before we play."));
        set_label_text(s_line2,app_language_text("芽伴：我会在这里等你。","Sprout: I'll wait right here."));
    } else if(state.unpair>=1 && state.unpair<=5) {
        set_label_text(s_line1,phase);
        set_label_text(s_line2,app_language_text("养成存档和网络设置都还在。","Pet saves and Wi-Fi settings are safe."));
    } else if((int32_t)(s_notice_until-ms)>0) {
        set_label_text(s_line1,app_language_text("宝仔：收到，轮子停好啦。","Bao: Got it. My wheels are stopped."));
        set_label_text(s_line2,app_language_text("芽伴：休息一下也很好。","Sprout: A little rest is lovely."));
    } else {
        label_format(s_line1,"%s%s",app_language_text("宝仔：","Bao: "),pet_companion_dialogue(true,app_language_is_english()));
        label_format(s_line2,"%s%s",app_language_text("芽伴：","Sprout: "),
            state.connected ? pet_companion_dialogue(false,app_language_is_english()) : app_language_text("还在路上，马上就到。","On my way to you."));
    }
    if(s_first) set_label_text(lv_obj_get_child(s_first,0),state.phase==3 ?
        (state.confirmed ? app_language_text("等你确认","Waiting") : app_language_text("成为伙伴","Be friends")) :
        state.bound ? app_language_text("招招手","Wave") : app_language_text("找伙伴","Find"));
    if(s_diag && ms-s_diag_tick>=500) {
        s_diag_tick=ms;
        label_format(s_diag,"v3 phase=%u hs=%u stop=%u\nbus=%lums ack=%lu/%lums\nTX=%lu noMem=%lu err=%ld\nRXdrop=%lu auth=%lu gap=%lu\nreconnect=%lu paused=%u",state.phase,state.handshake,state.reason,
            (unsigned long)state.business_age,(unsigned long)state.ack_seq,(unsigned long)state.ack_age,
            (unsigned long)state.tx_failed,(unsigned long)state.tx_nomem,(long)state.last_tx_error,
            (unsigned long)state.rx_dropped,(unsigned long)state.auth_failed,(unsigned long)state.poll_max_gap,
            (unsigned long)state.reconnects,(unsigned)state.input_paused);
    }
}
