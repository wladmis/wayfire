#include "cursor.hpp"
#include "core.hpp"
#include "input-manager.hpp"
#include "workspace-manager.hpp"
#include "debug.hpp"
#include "compositor-surface.hpp"

static void handle_pointer_button_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_button*> (data);
    if (!core->input->handle_pointer_button(ev))
    {
        wlr_seat_pointer_notify_button(core->input->seat, ev->time_msec,
            ev->button, ev->state);
    }

    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_motion_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_motion*> (data);
    core->input->handle_pointer_motion(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_motion_absolute_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_motion_absolute*> (data);
    core->input->handle_pointer_motion_absolute(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_axis_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_axis*> (data);
    core->input->handle_pointer_axis(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_swipe_begin_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_swipe_begin*> (data);
    core->input->handle_pointer_swipe_begin(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_swipe_update_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_swipe_update*> (data);
    core->input->handle_pointer_swipe_update(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_swipe_end_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_swipe_end*> (data);
    core->input->handle_pointer_swipe_end(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_pinch_begin_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_pinch_begin*> (data);
    core->input->handle_pointer_pinch_begin(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_pinch_update_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_pinch_update*> (data);
    core->input->handle_pointer_pinch_update(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_pinch_end_cb(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_event_pointer_pinch_end*> (data);
    core->input->handle_pointer_pinch_end(ev);
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

static void handle_pointer_frame_cb(wl_listener*, void *data)
{
    core->input->handle_pointer_frame();
    wlr_idle_notify_activity(core->protocols.idle, core->get_current_seat());
}

bool input_manager::handle_pointer_button(wlr_event_pointer_button *ev)
{
    mod_binding_key = 0;

    std::vector<std::function<void()>> callbacks;
    if (ev->state == WLR_BUTTON_PRESSED)
    {
        count_other_inputs++;

        GetTuple(gx, gy, core->get_cursor_position());
        auto output = core->get_output_at(gx, gy);
        core->focus_output(output);

        GetTuple(ox, oy, core->get_active_output()->get_cursor_position());

        auto mod_state = get_modifiers();
        for (auto& binding : bindings[WF_BINDING_BUTTON])
        {
            if (binding->output == core->get_active_output() &&
                binding->value->as_cached_button().matches(
                    {mod_state, ev->button}))
            {
                /* We must be careful because the callback might be erased,
                 * so force copy the callback into the lambda */
                auto callback = binding->call.button;
                callbacks.push_back([=] () {(*callback) (ev->button, ox, oy);});
            }
        }

        for (auto& binding : bindings[WF_BINDING_ACTIVATOR])
        {
            if (binding->output == core->get_active_output() &&
                binding->value->matches_button({mod_state, ev->button}))
            {
                /* We must be careful because the callback might be erased,
                 * so force copy the callback into the lambda */
                auto callback = binding->call.activator;
                callbacks.push_back([=] () {
                    (*callback) (ACTIVATOR_SOURCE_BUTTONBINDING, ev->button);
                });
            }
        }

        for (auto call : callbacks)
            call();
    }
    else
    {
        count_other_inputs--;
    }

    if (active_grab)
    {
        if (active_grab->callbacks.pointer.button)
            active_grab->callbacks.pointer.button(ev->button, ev->state);
    } else if (cursor_focus)
    {
        auto custom = wf_compositor_surface_from_surface(cursor_focus);
        if (custom)
            custom->on_pointer_button(ev->button, ev->state);
    }

    /* Do not send event to clients if any button binding used it. However, if
     * the binding was just a single button (no modifiers), forward it to the client.
     * This allows some use-cases like click-to-focus plugin. */
    return (!callbacks.empty() && get_modifiers());
}

void input_manager::update_cursor_focus(wayfire_surface_t *focus, int x, int y)
{
    if (focus && !can_focus_surface(focus))
        return;

    wayfire_compositor_surface_t *compositor_surface = wf_compositor_surface_from_surface(cursor_focus);
    if (compositor_surface)
        compositor_surface->on_pointer_leave();

    if (cursor_focus != focus)
        log_info("change cursor focus %p -> %p", cursor_focus, focus);

    cursor_focus = focus;
    if (focus && !wf_compositor_surface_from_surface(focus))
    {
        wlr_seat_pointer_notify_enter(seat, focus->surface, x, y);
    } else
    {
        wlr_seat_pointer_clear_focus(seat);
    }

    if ((compositor_surface = wf_compositor_surface_from_surface(focus)))
        compositor_surface->on_pointer_enter(x, y);
}

void input_manager::update_cursor_position(uint32_t time_msec, bool real_update)
{
    GetTuple(x, y, core->get_cursor_position());
    if (input_grabbed() && real_update)
    {
        GetTuple(sx, sy, core->get_active_output()->get_cursor_position());
        if (active_grab->callbacks.pointer.motion)
            active_grab->callbacks.pointer.motion(sx, sy);
        return;
    }

    int lx, ly;
    wayfire_surface_t *new_focus = input_surface_at(x, y, lx, ly);
    update_cursor_focus(new_focus, lx, ly);

    auto compositor_surface = wf_compositor_surface_from_surface(new_focus);
    if (compositor_surface)
    {
        compositor_surface->on_pointer_motion(lx, ly);
    }
    else if (real_update)
    {
        wlr_seat_pointer_notify_motion(core->input->seat, time_msec, lx, ly);
    }

    update_drag_icons();
}

void input_manager::handle_pointer_motion(wlr_event_pointer_motion *ev)
{
    if (input_grabbed() && active_grab->callbacks.pointer.relative_motion)
        active_grab->callbacks.pointer.relative_motion(ev);

    wlr_cursor_move(cursor->cursor, ev->device, ev->delta_x, ev->delta_y);
    update_cursor_position(ev->time_msec);
}

void input_manager::handle_pointer_motion_absolute(wlr_event_pointer_motion_absolute *ev)
{
    wlr_cursor_warp_absolute(cursor->cursor, ev->device, ev->x, ev->y);
    update_cursor_position(ev->time_msec);
}

void input_manager::handle_pointer_axis(wlr_event_pointer_axis *ev)
{
    std::vector<axis_callback*> callbacks;

    auto mod_state = get_modifiers();

    for (auto& binding : bindings[WF_BINDING_AXIS])
    {
        if (binding->output == core->get_active_output() &&
            binding->value->as_cached_key().matches({mod_state, 0}))
            callbacks.push_back(binding->call.axis);
    }

    for (auto call : callbacks)
        (*call) (ev);

    /* reset modifier bindings */
    mod_binding_key = 0;
    if (active_grab)
    {
        if (active_grab->callbacks.pointer.axis)
            active_grab->callbacks.pointer.axis(ev);

        return;
    }

    /* Do not send scroll events to clients if an axis binding has used up the event */
    if (callbacks.size())
        return;

    double mult = ev->source == WLR_AXIS_SOURCE_FINGER ?
        cursor->touchpad_scroll_speed->as_cached_double() :
        cursor->mouse_scroll_speed->as_cached_double();
    wlr_seat_pointer_notify_axis(seat, ev->time_msec, ev->orientation,
                                 mult * ev->delta, mult * ev->delta_discrete, ev->source);
}

void input_manager::handle_pointer_swipe_begin(wlr_event_pointer_swipe_begin *ev)
{
    wlr_pointer_gestures_v1_send_swipe_begin(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->fingers);
}

void input_manager::handle_pointer_swipe_update(wlr_event_pointer_swipe_update *ev)
{
    wlr_pointer_gestures_v1_send_swipe_update(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->dx, ev->dy);
}

void input_manager::handle_pointer_swipe_end(wlr_event_pointer_swipe_end *ev)
{
    wlr_pointer_gestures_v1_send_swipe_end(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->cancelled);
}

void input_manager::handle_pointer_pinch_begin(wlr_event_pointer_pinch_begin *ev)
{
    wlr_pointer_gestures_v1_send_pinch_begin(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->fingers);
}

void input_manager::handle_pointer_pinch_update(wlr_event_pointer_pinch_update *ev)
{
    wlr_pointer_gestures_v1_send_pinch_update(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->dx, ev->dy, ev->scale, ev->rotation);
}

void input_manager::handle_pointer_pinch_end(wlr_event_pointer_pinch_end *ev)
{
    wlr_pointer_gestures_v1_send_pinch_end(core->protocols.pointer_gestures, seat,
            ev->time_msec, ev->cancelled);
}

void input_manager::handle_pointer_frame()
{
    wlr_seat_pointer_notify_frame(seat);
}

wf_cursor::wf_cursor()
{
    cursor = wlr_cursor_create();

    wlr_cursor_attach_output_layout(cursor, core->output_layout);
    wlr_cursor_map_to_output(cursor, NULL);
    wlr_cursor_warp(cursor, NULL, cursor->x, cursor->y);

    button.notify             = handle_pointer_button_cb;
    motion.notify             = handle_pointer_motion_cb;
    motion_absolute.notify    = handle_pointer_motion_absolute_cb;
    axis.notify               = handle_pointer_axis_cb;
    swipe_begin.notify        = handle_pointer_swipe_begin_cb;
    swipe_update.notify       = handle_pointer_swipe_update_cb;
    swipe_end.notify          = handle_pointer_swipe_end_cb;
    pinch_begin.notify        = handle_pointer_pinch_begin_cb;
    pinch_update.notify       = handle_pointer_pinch_update_cb;
    pinch_end.notify          = handle_pointer_pinch_end_cb;
    frame.notify              = handle_pointer_frame_cb;

    wl_signal_add(&cursor->events.button, &button);
    wl_signal_add(&cursor->events.motion, &motion);
    wl_signal_add(&cursor->events.motion_absolute, &motion_absolute);
    wl_signal_add(&cursor->events.axis, &axis);
    wl_signal_add(&cursor->events.swipe_begin, &swipe_begin);
    wl_signal_add(&cursor->events.swipe_update, &swipe_update);
    wl_signal_add(&cursor->events.swipe_end, &swipe_end);
    wl_signal_add(&cursor->events.pinch_begin, &pinch_begin);
    wl_signal_add(&cursor->events.pinch_update, &pinch_update);
    wl_signal_add(&cursor->events.pinch_end, &pinch_end);
    wl_signal_add(&cursor->events.frame, &frame);

    init_xcursor();

    config_reloaded = [=] (signal_data*) {
        init_xcursor();
    };

    auto section = core->config->get_section("input");
    mouse_scroll_speed    = section->get_option("mouse_scroll_speed", "1");
    touchpad_scroll_speed = section->get_option("touchpad_scroll_speed", "1");

    core->connect_signal("reload-config", &config_reloaded);
}

void wf_cursor::init_xcursor()
{
    auto section = core->config->get_section("input");

    auto theme = section->get_option("cursor_theme", "default")->as_string();
    auto size = section->get_option("cursor_size", "24");

    auto theme_ptr = (theme == "default") ? NULL : theme.c_str();

    if (xcursor)
        wlr_xcursor_manager_destroy(xcursor);

    xcursor = wlr_xcursor_manager_create(theme_ptr, size->as_int());
    wlr_xcursor_manager_load(xcursor, 1);

    set_cursor("default");
}

void wf_cursor::attach_device(wlr_input_device *device)
{
    wlr_cursor_attach_input_device(cursor, device);
}

void wf_cursor::detach_device(wlr_input_device *device)
{
    wlr_cursor_detach_input_device(cursor, device);
}

void wf_cursor::set_cursor(std::string name)
{
    if (name == "default")
        name = "left_ptr";

    wlr_xcursor_manager_set_cursor_image(xcursor, name.c_str(), cursor);
}

void wf_cursor::hide_cursor()
{
    wlr_cursor_set_surface(cursor, NULL, 0, 0);
}

void wf_cursor::warp_cursor(int x, int y)
{
    wlr_cursor_warp(cursor, NULL, x, y);
    core->input->update_cursor_position(get_current_time());
}

void wf_cursor::set_cursor(wlr_seat_pointer_request_set_cursor_event *ev)
{
    auto focused_surface = ev->seat_client->seat->pointer_state.focused_surface;
    auto client = focused_surface ? wl_resource_get_client(focused_surface->resource) : NULL;

    if (client == ev->seat_client->client && !core->input->input_grabbed())
        wlr_cursor_set_surface(cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
}

wf_cursor::~wf_cursor()
{
    wl_list_remove(&button.link);
    wl_list_remove(&motion.link);
    wl_list_remove(&motion_absolute.link);
    wl_list_remove(&axis.link);
    wl_list_remove(&swipe_begin.link);
    wl_list_remove(&swipe_update.link);
    wl_list_remove(&swipe_end.link);
    wl_list_remove(&pinch_begin.link);
    wl_list_remove(&pinch_update.link);
    wl_list_remove(&pinch_end.link);
    wl_list_remove(&frame.link);

    core->disconnect_signal("reload-config", &config_reloaded);
}

