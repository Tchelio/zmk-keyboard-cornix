/*
 * 활성 레이어를 ZMK Studio RPC로 호스트에 알린다.
 *
 * SPDX-License-Identifier: MIT
 *
 * 배경: 원래는 레이어가 바뀔 때 안 쓰는 F키(F17/F18/F19/F13)를 눌러 보내는
 * 방식이었다(src/behavior_traceable.c, CONFIG_ZMK_LAYER_MARKER). 그런데 그건
 * 진짜 HID 키 입력이라 오버레이 앱만이 아니라 포커스된 모든 앱이 받았고,
 * macOS 터미널이 F13+를 ESC[NN~ 이스케이프로 해석해 sudo 비밀번호 프롬프트
 * 같은 raw 읽기를 망가뜨렸다(단순 문자 누출이 아니라 실제 인증 실패,
 * 마커 4개 전부에서 재현). 마우스 버튼으로 바꿔봐도 터미널의 마우스 리포팅
 * 때문에 같은 문제가 났다.
 *
 * 표준 HID 입력은 "포커스된 앱에 브로드캐스트"되는 것이 설계 의도이므로,
 * 안전한 키를 찾는 접근 자체가 틀렸다. 그래서 입력이 아닌 채널 - 이미 키맵
 * 편집에 쓰고 있는 Studio RPC - 로 옮긴다. 키 입력을 전혀 보내지 않으므로
 * 다른 앱이 영향을 받을 여지가 없다.
 *
 * 프로토콜: 새 서브시스템을 만들려면 ZMK 본체를 포크해야 한다(app/CMakeLists
 * .txt가 .proto 파일을 이름으로 나열해서 새 파일은 컴파일 대상에 안 들어간다).
 * 그래서 기존 core.proto에 필드를 얹었고, proto 저장소만 포크해서
 * config/west.yml에서 덮어쓴다. ZMK 엔진은 공식 main 그대로다.
 *   Tchelio/zmk-studio-messages: core.proto에 아래 3개 추가
 *     Request.get_active_layers / Response.get_active_layers
 *     Notification.active_layers_changed
 *
 * 전달 형식: 변화량("레이어 3이 켜졌다")이 아니라 매번 활성 레이어 비트마스크
 * 전체를 보낸다. 신호를 한 번 놓쳐도 다음 신호에 저절로 맞춰지므로 수신 측이
 * 상태를 따로 추적할 필요가 없고 어긋날 수도 없다. zmk_keymap_layers_state_t가
 * 이미 uint32_t 비트마스크라 변환 없이 그대로 실어 보낸다.
 *
 * 경위 전체는 vault의 keyboard-레이어상태-호스트전달채널-재설계 참조.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/studio/rpc.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * 요청 핸들러. 알림만 있으면 호스트 앱이 나중에 켜졌을 때 다음 레이어 전환이
 * 일어날 때까지 현재 레이어를 모르는 상태로 있게 되므로, 물어볼 수단을 함께 둔다.
 *
 * UNSECURED: 활성 레이어는 키맵 편집이 아니라 단순 상태 조회라 Studio 잠금
 * 해제를 요구하지 않는다(get_device_info / get_lock_state와 같은 등급).
 */
zmk_studio_Response get_active_layers(const zmk_studio_Request *req) {
    zmk_keymap_layers_state_t state = zmk_keymap_layer_state();

    LOG_DBG("get_active_layers: 0x%08x", (uint32_t)state);

    return ZMK_RPC_RESPONSE(core, get_active_layers, (uint32_t)state);
}

ZMK_RPC_SUBSYSTEM_HANDLER(core, get_active_layers, ZMK_STUDIO_RPC_HANDLER_UNSECURED);

/*
 * 레이어가 바뀌면 알림을 직접 올린다.
 *
 * 원래는 ZMK_RPC_EVENT_MAPPER로 등록했는데 실기기에서 알림이 한 개도 안 나갔다
 * (2026-08-30, 요청/응답은 정상 동작하는 같은 세션에서 확인). 매퍼 방식은
 * rpc.c의 studio_rpc 리스너가 등록된 매퍼들을 차례로 호출하는 구조인데,
 * ZMK의 keymap_subsystem.c가 이런 걸 등록해 두고 있다:
 *
 *   static int event_mapper(...) { return 0; }   // 무조건 "처리함"
 *   ZMK_RPC_EVENT_MAPPER(keymap, event_mapper);
 *
 * 매퍼 루프는 첫 번째로 0 이상을 반환한 매퍼에서 break하므로, 링크 순서상
 * 우리보다 앞에 있으면 우리 매퍼는 호출되지 않는다(src/studio/CMakeLists.txt
 * 기준 keymap_subsystem.c가 모듈 소스보다 먼저 링크된다).
 *
 * 그래서 매퍼에 의존하지 않고 자체 리스너에서 알림 이벤트를 직접 올린다.
 * raise_zmk_studio_rpc_notification은 zmk/studio/rpc.h가 공개하는 API이고,
 * rpc.c의 리스너가 그 이벤트를 받아 전송까지 처리한다. 이 저장소에서 이미
 * 검증된 패턴이기도 하다 - 기존 F키 마커(behavior_traceable.c)도 자체
 * ZMK_LISTENER/ZMK_SUBSCRIPTION으로 동작했다.
 *
 * 이벤트가 실어 나르는 ev->layer/ev->state는 쓰지 않고 zmk_keymap_layer_state()로
 * 현재 전체 상태를 다시 읽는다 - 알림 하나가 그 자체로 완전한 스냅샷이 되도록.
 */
static int layer_state_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_keymap_layers_state_t state = zmk_keymap_layer_state();

    LOG_DBG("layer %d %s, active layers now 0x%08x", ev->layer,
            ev->state ? "activated" : "deactivated", (uint32_t)state);

    raise_zmk_studio_rpc_notification((struct zmk_studio_rpc_notification){
        .notification = ZMK_RPC_NOTIFICATION(core, active_layers_changed, (uint32_t)state),
    });

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_state_rpc, layer_state_listener);
ZMK_SUBSCRIPTION(layer_state_rpc, zmk_layer_state_changed);
