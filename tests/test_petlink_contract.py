"""Pet Ring 联动的只读源码合约；不编译或执行固件。"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def sdk_source(text, major):
    """只展开本模块的 SDK 版本条件，检查实际选中分支，不调用编译器。"""
    result, stack = [], []
    active = True
    for line in text.splitlines():
        directive = line.strip()
        match = re.fullmatch(r"#if ESP_IDF_VERSION_MAJOR (>=|<) 6", directive)
        if match:
            condition = major >= 6 if match.group(1) == ">=" else major < 6
            stack.append((active, condition))
            active = active and condition
        elif directive == "#else":
            parent, condition = stack[-1]
            active = parent and not condition
        elif directive.startswith("#endif"):
            active, _ = stack.pop()
        elif directive.startswith(("#if", "#elif")):
            raise AssertionError("未识别的条件，需更新只读分支检查：" + directive)
        elif active:
            result.append(line)
    if stack:
        raise AssertionError("SDK 条件未闭合")
    return "\n".join(result)


class PetLinkContractTest(unittest.TestCase):
    def test_companion_is_a_game_card_not_a_launcher_app(self):
        game = source("main/page_game.c")
        self.assertIn("SUB_COMPANION", game)
        self.assertIn("game_pet_companion_build", game)
        self.assertIn("game_pet_companion_exit", game)
        self.assertNotIn("PAGE_PET_COMPANION", source("main/ui_page_manager.h"))
        self.assertNotIn("app_petlink", source("main/page_launcher.c"))

    def test_added_sources_are_in_build_manifest(self):
        manifest = source("main/CMakeLists.txt")
        for name in ("app_petlink.cpp", "game_pet_companion.c", "pet_companion_avatar.cpp"):
            self.assertIn(name, manifest)
        self.assertIn("petlink", manifest)
        self.assertIn("pet_avatar", manifest)

    def test_large_context_and_pixels_prefer_psram(self):
        adapter = source("main/app_petlink.cpp")
        page = source("main/game_pet_companion.c")
        self.assertIn("sizeof(petlink::Endpoint),MALLOC_CAP_SPIRAM", adapter)
        self.assertIn("xTaskCreatePinnedToCoreWithCaps", adapter)
        self.assertIn("tskNO_AFFINITY,MALLOC_CAP_INTERNAL", adapter)
        self.assertIn("COMPANION_STAGE_WIDTH*COMPANION_STAGE_HEIGHT", page)
        self.assertIn("MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT", page)
        header = source("main/game_pet_companion.h")
        self.assertIn("COMPANION_STAGE_WIDTH 256", header)
        self.assertIn("COMPANION_STAGE_HEIGHT 112", header)
        self.assertIn("lv_image_cache_drop", page)
        self.assertIn("heap_caps_free(s_pixels)", page)
        self.assertNotIn("320 * 320", page)

    def test_ring_drive_full_scale_without_hidden_axis_limits(self):
        adapter = source('main/app_petlink.cpp')
        self.assertIn('#include "PetLinkDrive.h"', adapter)
        self.assertNotIn('boundDriveAngular', adapter)
        self.assertNotIn('kDriveLinearExit', adapter)
        self.assertNotIn('linear=angular=0', adapter)
        self.assertIn('float(linear)*petlink::kRingLinearScale', adapter)
        self.assertIn('float(angular)*petlink::kRingAngularScale', adapter)
        drive = source('components/petlink/src/PetLinkDrive.h')
        self.assertIn('kRingLinearScale=0.001f', drive)
        self.assertIn('kRingAngularScale=0.001f', drive)
        for removed in ('DriveShaper', 'DriveMode', 'kDriveTurnNumerator', 'kDriveLinearExit'):
            self.assertNotIn(removed, drive)
        manager = source('main/motion_manager.c')
        self.assertIn('MOTION_RING_BREAKAWAY_PERCENT 40.0f', manager)
        self.assertIn('MOTION_FOLLOW_TURN_MIN_PERCENT 28.0f', manager)
        self.assertIn('return motion_math_ramp_motor(current, target,', manager)
        self.assertIn('motion_calibration_apply_wheels(&s_calibration, &mix)', manager)
        math_source = source('main/motion_math.c')
        self.assertIn('clampf(linear + turn_gain * angular', math_source)
        self.assertIn('clampf(linear - turn_gain * angular', math_source)

    def test_ring_motion_is_gated_and_does_not_write_pwm(self):
        adapter = source("main/app_petlink.cpp")
        manager = source("main/motion_manager.c")
        self.assertIn("MOTION_SOURCE_RING", adapter)
        self.assertIn("motion_manager_ring_session", adapter)
        self.assertIn("app_face_follow_stop()", adapter)
        self.assertNotIn("actuator_control_apply(", adapter)
        self.assertIn("s_ring_exclusive", manager)
        self.assertIn("!s_ring_exclusive && source == MOTION_SOURCE_RING", manager)
        self.assertIn("s_calibrating || s_ring_exclusive", manager)
        self.assertIn("app_petlink_stop()", source("main/ui_control_center.c"))

    def test_wifi_has_one_owner_and_offline_channel_coordination(self):
        adapter = source("main/app_petlink.cpp")
        wifi = source("main/wifi_provisioning.c")
        self.assertIn("wifi_prepare_petlink()", adapter)
        self.assertIn("wifi_petlink_enter()", adapter)
        self.assertIn("wifi_petlink_leave()", adapter)
        self.assertNotIn("esp_wifi_init(", adapter)
        self.assertIn("s_petlink_hold_reconnect", wifi)
        self.assertIn("esp_wifi_sta_get_ap_info", wifi)
        enter = wifi[wifi.index('esp_err_t wifi_petlink_enter'):wifi.index('void wifi_petlink_leave')]
        self.assertLess(enter.index('__atomic_store_n(&s_petlink_hold_reconnect, true'),
                        enter.index('esp_wifi_sta_get_ap_info(&ap)'))

    def test_protocol_has_fresh_sessions_authentication_and_timeout(self):
        core = source("components/petlink/src/PetLinkCore.h")
        link = source("components/petlink/src/PetLink.cpp") + source("components/petlink/src/PetLinkRuntime.inc")
        self.assertIn("kControlTimeout = 300", core)
        self.assertIn("kLinkTimeout = 2500", core)
        self.assertIn("kInputTimeout = 180", core)
        self.assertIn("kProtocolVersion = 3", core)
        self.assertIn("req==request", core)
        self.assertIn("sessionKey()", link)
        self.assertIn("mbedtls_ecdh_compute_shared", link)
        self.assertIn("mbedtls_md_hmac", link)
        self.assertIn("lastConfirmTx_", link)
        self.assertIn("elapsed(now,rx.received,maxAge)", link)
        self.assertIn("? kInputTimeout : 1200", link)
        self.assertIn("lease_.accepts", link)

    def test_idf6_uses_public_psa_api_without_openthread_headers(self):
        link = sdk_source(source("components/petlink/src/PetLink.cpp"), 6)
        self.assertIn('#include "psa/crypto.h"', link)
        self.assertNotIn('#include "mbedtls/ecdh.h"', link)
        self.assertNotIn('#include "mbedtls/md.h"', link)
        for api in ("psa_crypto_init", "psa_generate_key", "psa_export_public_key",
                    "psa_raw_key_agreement", "psa_destroy_key", "psa_import_key",
                    "psa_mac_compute", "psa_hash_compute"):
            self.assertIn(api, link)
        # 不再用“另一分支有此字符串”冒充 IDF 6 支持，明确检查选中分支的调用。
        mbedtls_calls = set(re.findall(r"\b(mbedtls_\w+)\s*\(", link))
        self.assertEqual(mbedtls_calls, {"mbedtls_platform_zeroize"})
        self.assertNotIn("MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS", link)
        self.assertNotIn("openthread", source("components/petlink/CMakeLists.txt"))

    def test_idf5_retains_legacy_crypto_without_psa_dependency(self):
        link = sdk_source(source("components/petlink/src/PetLink.cpp"), 5)
        self.assertIn('#include "mbedtls/ecdh.h"', link)
        self.assertIn('#include "mbedtls/md.h"', link)
        self.assertIn("mbedtls_ecdh_compute_shared", link)
        self.assertIn("mbedtls_md_hmac", link)
        self.assertNotIn('#include "psa/crypto.h"', link)
        self.assertEqual(re.findall(r"\bpsa_\w+\s*\(", link), [])

    def test_hmac_cache_is_bounded_and_released_on_failure_and_close(self):
        link = sdk_source(source("components/petlink/src/PetLink.cpp"), 6)
        self.assertIn("MacSlot slots[2]", link)
        self.assertIn("psa_destroy_key(slot.id)", link)
        self.assertIn("psa_destroy_key(selected->id)", link)
        hmac = link[link.index("bool hmac("):link.index("bool hkdf48(")]
        self.assertIn("PSA_KEY_USAGE_SIGN_MESSAGE", hmac)
        self.assertIn("PSA_ALG_HMAC(PSA_ALG_SHA_256)", hmac)
        self.assertIn("if(!selected)", hmac)
        self.assertLess(hmac.index("psa_import_key("), hmac.index("psa_mac_compute("))
        self.assertIn("resetMacCache(); return false;", hmac)
        runtime = source("components/petlink/src/PetLinkRuntime.inc")
        self.assertGreaterEqual(runtime.count("resetMacCache()"), 2)

    def test_image_cache_function_has_an_explicit_header(self):
        page = source("main/game_pet_companion.c")
        self.assertIn('#include "src/misc/cache/instance/lv_image_cache.h"', page)
        self.assertEqual(page.count("lv_image_cache_drop(&s_descriptor)"), 2)
        self.assertNotIn("#define LV_USE_PRIVATE_API", page)

    def test_pairing_key_is_volatile_and_does_not_export_private_material(self):
        link = source("components/petlink/src/PetLink.cpp")
        self.assertIn("PSA_KEY_LIFETIME_VOLATILE", link)
        self.assertIn("PSA_KEY_USAGE_DERIVE", link)
        self.assertIn("PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)", link)
        self.assertIn("written==65 && offer[0]==0x04", link)
        self.assertNotIn("psa_export_key(", link)
        self.assertNotIn("mbedtls_psa_crypto_free(", link)

    def test_stop_cancels_queued_interaction_and_unpair_is_a_click(self):
        page = source("main/game_pet_companion.c")
        self.assertNotIn("LV_EVENT_LONG_PRESSED", page)
        self.assertIn("confirm_forget_cb,LV_EVENT_CLICKED", page)
        self.assertIn("s_pending_kind=0; pet_companion_cancel(); app_petlink_stop();", page)
        adapter = source("main/app_petlink.cpp")
        self.assertIn("if(action && !(cmd&(2|16|32)))", adapter)
        self.assertIn("err=radio; retryAt=now+3000", adapter)

    def test_runtime_separates_ack_epochs_and_timing(self):
        runtime = source("components/petlink/src/PetLinkRuntime.inc")
        self.assertIn("(connected_ || probing_) && !pairing_) return", runtime)
        self.assertIn("lastControlTx_,40", runtime)
        self.assertIn("lastHeartbeat_,500", runtime)
        self.assertIn("lastTx_,100", runtime)
        self.assertIn("uint8_t status[44]", runtime)
        self.assertIn("p.length==44", runtime)
        self.assertIn("uint8_t grant[32]", runtime)
        self.assertIn("p.length==32", runtime)
        self.assertIn("get32(p.data+16)==sceneEpoch_", runtime)
        self.assertIn("p.length==4 && get32(p.data)==sceneEpoch_", runtime)
        self.assertIn("current=newEpoch ||", runtime)
        self.assertIn("uint8_t event[16]", runtime)
        self.assertIn("p.length!=16", runtime)
        self.assertIn("ack==armFirstSeq_ || newer(ack,armFirstSeq_)", runtime)
        self.assertIn("get32(p.data+12)!=sceneEpoch_", runtime)
        self.assertIn("!eraseBond()", runtime)
        self.assertNotIn("nvs_erase_all", runtime)

    def test_dialogue_never_scrolls_and_fits_real_font_height(self):
        page = source('main/game_pet_companion.c')
        self.assertNotIn('LV_LABEL_LONG_SCROLL', page)
        self.assertIn('lv_font_get_line_height(&font_puhui_16_4)', page)
        self.assertIn('278,line_height', page)
        self.assertIn('LV_LABEL_LONG_CLIP', page)
        self.assertIn('lv_font_get_glyph_width(&font_puhui_16_4', page)
        self.assertIn('dialogue_page(value,(now_ms()/2400)%pages,page)', page)
        font = ROOT / 'managed_components/78__xiaozhi-fonts/src/font_puhui_16_4.c'
        if font.is_file():
            height = int(re.search(r'\.line_height\s*=\s*(\d+)', font.read_text(encoding='utf-8'))[1])
            bubble = re.search(r'lv_obj_set_size\(bubble,292,(\d+)\)', page)
            self.assertGreaterEqual(int(bubble[1]), height*2+2)
        self.assertIn('LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN', page)

    def test_new_diagnostics_distinguish_link_input_and_control(self):
        adapter = source('main/app_petlink.cpp')
        for field in ('s.inputPaused', 's.handshake', 's.businessAge', 's.authFailed', 's.txNoMem', 's.controlAck'):
            self.assertIn(field, adapter)
        self.assertIn('vTaskDelayUntil', adapter)
        self.assertIn('state.input_paused', source('main/game_pet_companion.c'))

    def test_companion_c_uses_c_cast_syntax(self):
        page = source("main/game_pet_companion.c")
        self.assertIn("(int32_t)(s_notice_until-ms)>0", page)
        # .c 仍由 C 编译器处理，不能混入 C++ 的 Type(value) 转换语法。
        self.assertNotRegex(page, r"\b(?:u?int(?:8|16|32|64)_t|size_t|bool|float|double)\s*\(")

    def test_game_theme_and_stop_area_do_not_depend_on_popup(self):
        page = source("main/game_pet_companion.c")
        self.assertIn("PET_THEME_PAPER", page)
        self.assertIn("PET_THEME_INK", page)
        self.assertIn("lv_obj_set_size(s_popup,296,208)", page)
        # 弹层到 y256 为止；停止区从 y266 开始，不被遮挡。
        self.assertIn("lv_obj_set_pos(s_popup,12,48)", page)
        self.assertIn("button(root,14,266,292,46", page)
        self.assertIn("LV_EVENT_PRESSED", page)

    def test_protocol_vendored_copy_matches_ring_when_available(self):
        ring = ROOT.parent / "T-QT-C6-main" / "libraries/PetLink/src"
        if not ring.is_dir():
            self.skipTest("当前 checkout 不包含相邻指环仓库")
        for name in ("PetLinkCore.h", "PetLinkDrive.h", "PetLink.h", "PetLink.cpp", "PetLinkRuntime.inc"):
            self.assertEqual((ring / name).read_bytes(),
                             (ROOT / "components/petlink/src" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
