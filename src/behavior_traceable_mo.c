/*
 * Momentary layer, identical to the stock &mo, except it also emits an
 * unused F-key (F20 + layer number) as an invisible marker on press.
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK Studio's RPC exposes only core/behaviors/keymap and has no key-event or
 * layer-state notification at all - a desktop app has no way to ask which
 * layer is currently active. This behavior sidesteps that the same way
 * scylla-zmk-config's behavior_status_report.c does for connection status:
 * volunteer the information over the channel the keyboard always has, the
 * keystrokes it is already sending.
 *
 * F20-F23 have no default binding in any mainstream OS/app, so the marker
 * produces no visible effect during normal typing - unlike status_report,
 * which types a readable string and would be disruptive if fired on every
 * layer press instead of on demand.
 *
 * Layer activation itself (zmk_keymap_layer_activate/deactivate) is
 * unchanged from ZMK's own behavior_momentary_layer.c.
 */

#define DT_DRV_COMPAT zmk_behavior_traceable_mo

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_traceable_mo_config {
    bool locking;
};

/* layer 1 -> F20, 2 -> F21, 3 -> F22, 4 -> F23. Numbers/L2/L3/L4 = layers
 * 1-4 today, so this covers all four with room for one more (F24) if a
 * fifth ever gets added. */
static uint16_t marker_usage_for_layer(uint8_t layer) {
    return HID_USAGE_KEY_KEYBOARD_F20 + (layer - 1);
}

static int traceable_mo_binding_pressed(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct behavior_traceable_mo_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;

    uint32_t marker = ((uint32_t)HID_USAGE_KEY << 16) | marker_usage_for_layer(binding->param1);
    raise_zmk_keycode_state_changed_from_encoded(marker, true, event.timestamp);
    raise_zmk_keycode_state_changed_from_encoded(marker, false, event.timestamp);

    LOG_DBG("traceable_mo: position %d layer %d marker 0x%02x", event.position, binding->param1,
            marker_usage_for_layer(binding->param1));

    return zmk_keymap_layer_activate(binding->param1, cfg->locking);
}

static int traceable_mo_binding_released(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct behavior_traceable_mo_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;
    return zmk_keymap_layer_deactivate(binding->param1, cfg->locking);
}

static const struct behavior_driver_api behavior_traceable_mo_driver_api = {
    .binding_pressed = traceable_mo_binding_pressed,
    .binding_released = traceable_mo_binding_released,
};

#define TMO_INST(n)                                                                       \
    static const struct behavior_traceable_mo_config behavior_traceable_mo_config_##n = { \
        .locking = DT_INST_PROP_OR(n, locking, false),                                    \
    };                                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_traceable_mo_config_##n,        \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &behavior_traceable_mo_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TMO_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
