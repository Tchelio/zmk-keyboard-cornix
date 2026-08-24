/*
 * Layer-change marker: listens for ZMK's own zmk_layer_state_changed event
 * (raised by zmk_keymap_layer_activate/deactivate inside keymap.c for every
 * layer-referencing behavior - &mo, &lt, &tog, &to, &sl, all of them) and
 * emits an unused F-key as an invisible marker whenever a layer is activated.
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
 * NOTE(2026-08-25): marker is held for as long as the layer is active (down
 * on activate, up on deactivate) instead of firing a down+up pulse at
 * activation - releasing the physical layer key normally releases the
 * marker too.
 *
 * NOTE(2026-08-25 #2): marker usage went through three rounds before landing
 * on F17/F18/F19/F13 -
 *   - F21-F24 first: no official macOS virtual keycode at all (confirmed
 *     against Chromium's keycode_conversion_mac.mm) - CGEventTap-based tools
 *     (the overlay app included) see them all collapse to one
 *     indistinguishable code, can't tell layers apart.
 *   - F13-F16: real virtual keycodes, distinguishable - but F14/F15 default
 *     to brightness down/up on macOS.
 *   - F17-F20: also real and distinguishable - but F20 acts as terminal
 *     Up-Arrow (shell history recall) on the dev machine.
 *   - Final set skips both problem keys: F17/F18/F19 (clean) + F13 (already
 *     confirmed clean in round 1). All four verified against real hardware.
 * This was first made to work via a Karabiner-Elements remap (F21->F17 etc.)
 * sitting in front of the overlay app's CGEventTap, but emitting the
 * already-safe usage directly here removes that dependency entirely.
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

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
