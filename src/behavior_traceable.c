/*
 * Layer-change marker: listens for ZMK's own zmk_layer_state_changed event
 * (raised by zmk_keymap_layer_activate/deactivate inside keymap.c for every
 * layer-referencing behavior - &mo, &lt, &tog, &to, &sl, all of them) and
 * emits an unused F-key (F20 + layer number) as an invisible marker whenever
 * a layer is activated.
 *
 * SPDX-License-Identifier: MIT
 *
 * NOTE(2026-08-25): originally implemented as a custom &traceable_mo
 * behavior that replaced &mo in the keymap. Replaced with this plain event
 * listener for two reasons:
 *   1. It's simpler - the keymap stays 100% stock &mo, no custom behavior/
 *      devicetree binding needed, no risk of forgetting to swap &mo for
 *      &traceable_mo on a new layer key.
 *   2. The web keymap editor (nickcoutsos/keymap-editor) only recognizes a
 *      hardcoded list of ~19 known ZMK behaviors (its api/services/zmk/data/
 *      zmk-behaviors.json) - a custom compatible like the old
 *      zmk,behavior-traceable-mo was invisible to it (not even a raw param
 *      box, confirmed against the real source). Keeping stock &mo sidesteps
 *      that limitation entirely.
 *
 * ZMK Studio's RPC exposes only core/behaviors/keymap and has no key-event or
 * layer-state notification at all - a desktop app has no way to ask which
 * layer is currently active. This sidesteps that the same way
 * scylla-zmk-config's behavior_status_report.c does for connection status:
 * volunteer the information over the channel the keyboard always has, the
 * keystrokes it is already sending.
 *
 * F21-F24 have no default binding in any mainstream OS/app, so the marker
 * produces no visible effect during normal typing.
 *
 * NOTE(2026-08-25): marker is now held for as long as the layer is active
 * (down on activate, up on deactivate) instead of firing a down+up pulse at
 * activation - releasing the physical layer key normally releases the
 * marker too. Also shifted the mapping from F20-F23 to F21-F24 so layer 1
 * doesn't map to a key that reads like "zero".
 *
 * NOTE(2026-08-25 #2): shifted again, F21-F24 -> F17/F18/F19/F13. Verified
 * against real macOS source (Chromium's keycode_conversion_mac.mm) that
 * macOS has no virtual keycode at all for F21-F24 - CGEventTap-based tools
 * (the overlay app included) see them all collapse to one indistinguishable
 * code. F1-F20 all have real virtual keycodes and ARE distinguishable, but
 * F14/F15 default to brightness down/up and F20 acts as terminal Up-Arrow
 * (shell history recall) on the dev machine - both confirmed by hitting them
 * during testing. F13/F17/F18/F19 tested clean. Originally worked around via
 * a Karabiner-Elements remap (F21->F17 etc.) sitting in front of CGEventTap,
 * but emitting the already-safe usage directly removes that dependency
 * entirely - the overlay app needs nothing but this firmware to work now.
 */
static uint16_t marker_usage_for_layer(uint8_t layer) {
    static const uint16_t usages[] = {
        HID_USAGE_KEY_KEYBOARD_F17,
        HID_USAGE_KEY_KEYBOARD_F18,
        HID_USAGE_KEY_KEYBOARD_F19,
        HID_USAGE_KEY_KEYBOARD_F13,
    };
    return usages[layer - 1];
}

static int layer_marker_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev == NULL || ev->layer == 0 || ev->layer > 4) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint32_t marker = ((uint32_t)HID_USAGE_KEY << 16) | marker_usage_for_layer(ev->layer);
    raise_zmk_keycode_state_changed_from_encoded(marker, ev->state, ev->timestamp);

    LOG_DBG("layer_marker: layer %d %s, marker 0x%02x", ev->layer,
            ev->state ? "activated" : "deactivated", marker_usage_for_layer(ev->layer));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_marker, layer_marker_listener);
ZMK_SUBSCRIPTION(layer_marker, zmk_layer_state_changed);
