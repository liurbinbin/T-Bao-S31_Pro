#include "PetLink.h"
#include <new>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 6
#include "psa/crypto.h"
#else
#include "mbedtls/ecdh.h"
#include "mbedtls/md.h"
#endif
#include "mbedtls/platform_util.h"

namespace petlink {
namespace {
constexpr uint8_t kBroadcast[6]={255,255,255,255,255,255};
constexpr size_t kQueueDepth=12;
uint32_t clockMs() { return uint32_t(esp_timer_get_time()/1000); }
#if ESP_IDF_VERSION_MAJOR < 6
int rng(void*,unsigned char* p,size_t n) { esp_fill_random(p,n); return 0; }
#endif
void* allocate(size_t n,bool external=true) {
  void* p=external ? heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) : nullptr;
  return p ? p : heap_caps_calloc(1,n,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
bool sha256(const uint8_t* data,size_t n,uint8_t* out) {
#if ESP_IDF_VERSION_MAJOR >= 6
  size_t written=0;
  return psa_hash_compute(PSA_ALG_SHA_256,data,n,out,32,&written)==PSA_SUCCESS && written==32;
#else
  const auto* md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return md && mbedtls_md(md,data,n,out)==0;
#endif
}
#if ESP_IDF_VERSION_MAJOR >= 6
struct MacSlot {
  uint8_t key[32]{};
  mbedtls_svc_key_id_t id=MBEDTLS_SVC_KEY_ID_INIT;
  bool valid=false;
};
struct MacCache { MacSlot slots[2]; unsigned next=0; };
MacCache* macCache=nullptr;
void resetMacCache() {
  if(!macCache) return;
  for(auto& slot:macCache->slots) if(slot.valid) psa_destroy_key(slot.id);
  mbedtls_platform_zeroize(macCache,sizeof(MacCache));
  heap_caps_free(macCache); macCache=nullptr;
}
#else
void resetMacCache() {}
#endif
bool hmac(const uint8_t* key,const uint8_t* data,size_t n,uint8_t* out) {
#if ESP_IDF_VERSION_MAJOR >= 6
  // 最多缓存根密钥与会话密钥两个临时句柄，周期帧不重复导入/释放。
  if(!macCache) {
    void* memory=allocate(sizeof(MacCache));
    if(!memory) return false;
    macCache=new(memory) MacCache();
  }
  MacSlot* selected=nullptr;
  for(auto& slot:macCache->slots) if(slot.valid && memcmp(slot.key,key,32)==0) selected=&slot;
  if(!selected) {
    selected=&macCache->slots[macCache->next++%2];
    if(selected->valid) {
      if(psa_destroy_key(selected->id)!=PSA_SUCCESS) return false;
      selected->valid=false;
      mbedtls_platform_zeroize(selected->key,32);
    }
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_HMAC); psa_set_key_bits(&attributes,256);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes,PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_lifetime(&attributes,PSA_KEY_LIFETIME_VOLATILE);
    const psa_status_t imported=psa_import_key(&attributes,key,32,&selected->id);
    psa_reset_key_attributes(&attributes);
    if(imported!=PSA_SUCCESS) return false;
    memcpy(selected->key,key,32); selected->valid=true;
  }
  size_t written=0;
  const psa_status_t computed=psa_mac_compute(selected->id,PSA_ALG_HMAC(PSA_ALG_SHA_256),data,n,out,32,&written);
  if(computed!=PSA_SUCCESS || written!=32) { resetMacCache(); return false; }
  return true;
#else
  const auto* md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return md && mbedtls_md_hmac(md,key,32,data,n,out)==0;
#endif
}
// RFC 5869：SHA256 extract + 两个 expand block，避免依赖 SDK 可选 HKDF 模块。
bool hkdf48(const uint8_t* salt,const uint8_t* secret,const uint8_t* info,size_t infoSize,uint8_t* output) {
  if(infoSize>31) return false;
  uint8_t prk[32]{},block[32]{},message[64]{};
  bool ok=hmac(salt,secret,32,prk);
  memcpy(message,info,infoSize); message[infoSize]=1;
  ok=ok && hmac(prk,message,infoSize+1,block);
  if(ok) {
    memcpy(output,block,32); memcpy(message,block,32);
    memcpy(message+32,info,infoSize); message[32+infoSize]=2;
    ok=hmac(prk,message,33+infoSize,block);
    if(ok) memcpy(output+32,block,16);
  }
  mbedtls_platform_zeroize(prk,sizeof(prk)); mbedtls_platform_zeroize(block,sizeof(block));
  mbedtls_platform_zeroize(message,sizeof(message)); return ok;
}
bool rootMessage(Type type) {
  return type==Type::Offer || type==Type::Confirm || type==Type::Discover || type==Type::Welcome;
}
bool equal(const uint8_t* a,const uint8_t* b,size_t n) {
  uint8_t diff=0; for(size_t i=0;i<n;++i) diff |= a[i]^b[i]; return diff==0;
}
}
struct Endpoint::Crypto {
  uint8_t offer[82]{},remote[82]{};
  bool derived=false;
#if ESP_IDF_VERSION_MAJOR >= 6
  // IDF 6 / Mbed TLS 4 只保管临时 PSA 句柄，不导出私钥或创建持久密钥。
  mbedtls_svc_key_id_t privateKey=MBEDTLS_SVC_KEY_ID_INIT;
  bool generated=false;
  ~Crypto() { if(generated) psa_destroy_key(privateKey); }
  bool generate() {
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes,PSA_ALG_ECDH);
    psa_set_key_lifetime(&attributes,PSA_KEY_LIFETIME_VOLATILE);
    const psa_status_t status=psa_generate_key(&attributes,&privateKey);
    psa_reset_key_attributes(&attributes);
    if(status!=PSA_SUCCESS) return false;
    generated=true;
    size_t written=0;
    return psa_export_public_key(privateKey,offer,65,&written)==PSA_SUCCESS &&
      written==65 && offer[0]==0x04;
  }
  bool agree(const uint8_t* peerKey,uint8_t* output) {
    size_t written=0;
    // PSA 校验对端公钥，输出与旧接口相同的 32 字节大端共享坐标。
    return generated && psa_raw_key_agreement(PSA_ALG_ECDH,privateKey,peerKey,65,
      output,32,&written)==PSA_SUCCESS && written==32;
  }
#else
  // Arduino-ESP32 3.x / IDF 5.x 保留原有 Mbed TLS 3 接口。
  mbedtls_ecp_group group;
  mbedtls_mpi secret;
  mbedtls_ecp_point publicKey;
  Crypto() { mbedtls_ecp_group_init(&group); mbedtls_mpi_init(&secret); mbedtls_ecp_point_init(&publicKey); }
  ~Crypto() { mbedtls_ecp_point_free(&publicKey); mbedtls_mpi_free(&secret); mbedtls_ecp_group_free(&group); }
  bool generate() {
    size_t written=0;
    return mbedtls_ecp_group_load(&group,MBEDTLS_ECP_DP_SECP256R1)==0 &&
      mbedtls_ecdh_gen_public(&group,&secret,&publicKey,rng,nullptr)==0 &&
      mbedtls_ecp_point_write_binary(&group,&publicKey,MBEDTLS_ECP_PF_UNCOMPRESSED,
        &written,offer,65)==0 && written==65;
  }
  bool agree(const uint8_t* peerKey,uint8_t* output) {
    mbedtls_ecp_point other; mbedtls_ecp_point_init(&other);
    mbedtls_mpi shared; mbedtls_mpi_init(&shared);
    const bool ok=mbedtls_ecp_point_read_binary(&group,&other,peerKey,65)==0 &&
      mbedtls_ecp_check_pubkey(&group,&other)==0 &&
      mbedtls_ecdh_compute_shared(&group,&shared,&other,&secret,rng,nullptr)==0 &&
      mbedtls_mpi_write_binary(&shared,output,32)==0;
    mbedtls_mpi_free(&shared); mbedtls_ecp_point_free(&other); return ok;
  }
#endif
};
Endpoint* Endpoint::instance_=nullptr;
portMUX_TYPE Endpoint::mux_=portMUX_INITIALIZER_UNLOCKED;
uint64_t Endpoint::randomId() { uint64_t n; do { esp_fill_random(&n,sizeof(n)); } while(!n); return n; }

esp_err_t Endpoint::begin(bool car,const Callbacks& cb) {
  if (ready_) return resumeRadio();
  if (instance_) return ESP_ERR_INVALID_STATE;
#if ESP_IDF_VERSION_MAJOR >= 6
  // 幂等初始化；绝不释放其他 TLS/PSA 使用者共享的全局密码库状态。
  const psa_status_t cryptoStatus=psa_crypto_init();
  if(cryptoStatus!=PSA_SUCCESS)
    return cryptoStatus==PSA_ERROR_INSUFFICIENT_MEMORY ? ESP_ERR_NO_MEM : ESP_FAIL;
#endif
  car_=car; callbacks_=cb; boot_=randomId(); link_=randomId();
  queueStorage_=static_cast<uint8_t*>(allocate(kQueueDepth*sizeof(Rx)));
  queueControl_=static_cast<StaticQueue_t*>(allocate(sizeof(StaticQueue_t),false));
  if (!queueStorage_ || !queueControl_) {
    heap_caps_free(queueStorage_); heap_caps_free(queueControl_);
    queueStorage_=nullptr; queueControl_=nullptr; return ESP_ERR_NO_MEM;
  }
  queue_=xQueueCreateStatic(kQueueDepth,sizeof(Rx),queueStorage_,queueControl_);
  esp_err_t err=esp_wifi_get_mac(WIFI_IF_STA,mac_);
  if (err==ESP_OK) err=esp_now_init();
  if (err!=ESP_OK) {
    if(queue_) vQueueDelete(queue_);
    heap_caps_free(queueStorage_); heap_caps_free(queueControl_);
    queue_=nullptr; queueStorage_=nullptr; queueControl_=nullptr; return err;
  }
  wifi_country_t country{};
  if (esp_wifi_get_country(&country)==ESP_OK && country.schan && country.nchan) {
    firstChannel_=country.schan; channelCount_=country.nchan;
  }
  channel_=firstChannel_;
  nvs_handle_t nvs;
  if (nvs_open("petlink",NVS_READONLY,&nvs)==ESP_OK) {
    size_t n=sizeof(bond_);
    bound_=nvs_get_blob(nvs,"bond",&bond_,&n)==ESP_OK && n==sizeof(bond_) && bond_.version==1;
    nvs_close(nvs);
  }
  instance_=this;
  err=esp_now_register_recv_cb(receive);
  if(err==ESP_OK) err=esp_now_register_send_cb(sent);
  if (err==ESP_OK && !peer(kBroadcast,nullptr)) err=ESP_FAIL;
  if (err==ESP_OK && bound_) {
    err=esp_now_set_pmk(bond_.auth);
    if(err==ESP_OK && !peer(bond_.mac,bond_.lmk)) err=ESP_FAIL;
  }
  if (err!=ESP_OK) {
    esp_now_unregister_recv_cb(); esp_now_unregister_send_cb(); esp_now_deinit(); instance_=nullptr;
    vQueueDelete(queue_); heap_caps_free(queueStorage_); heap_caps_free(queueControl_);
    queue_=nullptr; queueStorage_=nullptr; queueControl_=nullptr; return err;
  }
  ready_=true; state_.phase=Phase::Off; publish(clockMs()); return ESP_OK;
}
esp_err_t Endpoint::resumeRadio() {
  if(!ready_) return ESP_ERR_INVALID_STATE;
  // 无线电停止/重启后重建 ESP-NOW，而不是把旧 ready_ 当作驱动仍可用。
  esp_now_unregister_recv_cb(); esp_now_unregister_send_cb(); esp_now_deinit();
  esp_err_t error=esp_now_init();
  if(error==ESP_OK) error=esp_now_register_recv_cb(receive);
  if(error==ESP_OK) error=esp_now_register_send_cb(sent);
  if(error==ESP_OK && !peer(kBroadcast,nullptr)) error=ESP_FAIL;
  if(error==ESP_OK && bound_) error=esp_now_set_pmk(bond_.auth);
  if(error!=ESP_OK) { esp_now_unregister_recv_cb(); esp_now_unregister_send_cb(); esp_now_deinit(); }
  flushRx(); resetMacCache();
  return error;
}
void Endpoint::receive(const esp_now_recv_info_t* info,const uint8_t* data,int n) {
  Endpoint* self=instance_;
  if (!self || !info || !data || n<int(kHeader) || n>int(kMaxFrame)) return;
  // Wi-Fi 回调不分配内存、不写存档，也不执行运动或密码协商。
  Rx item{}; memcpy(item.mac,info->src_addr,6); item.size=uint16_t(n);
  item.received=clockMs(); memcpy(item.bytes,data,n);
  if(data[0]=='P' && data[1]=='R' && data[2]==kProtocolVersion && data[3]==uint8_t(Type::Control)) {
    portENTER_CRITICAL(&mux_);
    if(self->controlWaiting_) ++self->callbackDrops_;
    self->latestControl_=item; self->controlWaiting_=true;
    portEXIT_CRITICAL(&mux_);
  } else if(xQueueSend(self->queue_,&item,0)!=pdTRUE) {
    portENTER_CRITICAL(&mux_); ++self->callbackDrops_; portEXIT_CRITICAL(&mux_);
  }
}
void Endpoint::sent(const esp_now_send_info_t*,esp_now_send_status_t status) {
  Endpoint* self=instance_;
  if(self && status!=ESP_NOW_SEND_SUCCESS) {
    portENTER_CRITICAL(&mux_); ++self->callbackTxErrors_; portEXIT_CRITICAL(&mux_);
  }
}
bool Endpoint::peer(const uint8_t* mac,const uint8_t* key) {
  esp_now_peer_info_t p{}; memcpy(p.peer_addr,mac,6);
  p.ifidx=WIFI_IF_STA; p.channel=0; p.encrypt=key!=nullptr;
  if(key) memcpy(p.lmk,key,16);
  return (esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&p) : esp_now_add_peer(&p))==ESP_OK;
}
void Endpoint::send(Type type,const uint8_t* data,uint16_t length,bool broadcast,bool bootstrap) {
  const bool auth=!bootstrap;
  const uint64_t session=(type==Type::Confirm) ? pairId_ : link_;
  size_t n=header(tx_,type,session,++seq_,length,auth);
  if(!n) return;
  if(length) memcpy(tx_+kHeader,data,length);
  uint8_t tag[32];
  if(auth) {
    const uint8_t* key=rootMessage(type) ? bond_.auth : sessionKey_;
    if(!hmac(key,tx_,n,tag)) { ++txErrors_; return; }
    memcpy(tx_+n,tag,kTag); n+=kTag;
  }
  const esp_err_t error=esp_now_send(broadcast ? kBroadcast : bond_.mac,tx_,n);
  if(error!=ESP_OK) {
    ++txErrors_; state_.lastTxError=error;
    if(error==ESP_ERR_ESPNOW_NO_MEM) ++state_.txNoMem;
  }
}
bool Endpoint::storeBond() {
  nvs_handle_t nvs;
  if(nvs_open("petlink",NVS_READWRITE,&nvs)!=ESP_OK) return false;
  bond_.version=1;
  esp_err_t err=nvs_set_blob(nvs,"bond",&bond_,sizeof(bond_));
  if(err==ESP_OK) err=nvs_commit(nvs);
  nvs_close(nvs); return err==ESP_OK;
}
void Endpoint::endPair() {
  pairing_=false;
  if(!bound_ && crypto_ && crypto_->derived) {
    esp_now_del_peer(bond_.mac);
    mbedtls_platform_zeroize(&bond_,sizeof(bond_));
  }
  if(crypto_) {
    crypto_->~Crypto(); mbedtls_platform_zeroize(crypto_,sizeof(Crypto)); heap_caps_free(crypto_); crypto_=nullptr;
  }
}
bool Endpoint::startPair(uint32_t now) {
  if(bound_) return false; // 已绑定设备必须先显式解除，不能静默覆盖。
  endPair(); connected_=false; disarm();
  void* memory=allocate(sizeof(Crypto)); if(!memory) return false;
  crypto_=new(memory) Crypto();
  if(!crypto_->generate()) { endPair(); return false; }
  esp_fill_random(crypto_->offer+65,16); crypto_->offer[81]=car_ ? 1 : 0;
  localConfirmed_=remoteConfirmed_=false; pairId_=0;
  pairing_=true; pairUntil_=now+60000; lastTx_=now-250;
  state_.code=0; state_.error=ESP_OK; state_.unpair=UnpairState::None;
  state_.mismatch=false; state_.phase=Phase::Pairing; return true;
}
bool Endpoint::derive(const Rx& rx,const Packet& p) {
  if(!crypto_ || p.length!=82 || p.data[81]!=(car_ ? 0 : 1)) return false;
  if(crypto_->derived) return equal(rx.mac,bond_.mac,6) && equal(p.data,crypto_->remote,82);
  uint8_t secret[32]{},transcript[176]{},digest[32]{},keys[48]{};
  bool ok=crypto_->agree(p.data,secret);
  // 固定 ring→car 顺序，绑定角色、双方 MAC、公钥和随机数，防止反射与旧确认重放。
  memcpy(transcript,car_ ? p.data : crypto_->offer,82);
  memcpy(transcript+82,car_ ? crypto_->offer : p.data,82);
  memcpy(transcript+164,car_ ? rx.mac : mac_,6);
  memcpy(transcript+170,car_ ? mac_ : rx.mac,6);
  const uint8_t label[]="PetLink-v1-ECDH";
  ok=ok && sha256(transcript,sizeof(transcript),digest) &&
    hkdf48(digest,secret,label,sizeof(label)-1,keys);
  if(ok) {
    memcpy(bond_.mac,rx.mac,6); memcpy(bond_.lmk,keys,16); memcpy(bond_.auth,keys+16,32);
    memcpy(crypto_->remote,p.data,82); pairId_=get64(digest); if(!pairId_) pairId_=1;
    uint8_t sas[32]; ok=hmac(bond_.auth,digest,32,sas);
    if(ok) {
      state_.code=get32(sas)%1000000;
      ok=esp_now_set_pmk(bond_.auth)==ESP_OK && peer(bond_.mac,bond_.lmk);
    }
    if(ok) { crypto_->derived=true; state_.phase=Phase::Confirm; }
  }
  mbedtls_platform_zeroize(secret,sizeof(secret)); mbedtls_platform_zeroize(keys,sizeof(keys));
  return ok;
}
#include "PetLinkRuntime.inc"
} // namespace petlink
