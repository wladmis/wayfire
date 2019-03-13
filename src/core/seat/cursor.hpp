#ifndef CURSOR_HPP
#define CURSOR_HPP

#include "seat.hpp"
#include "plugin.hpp"

extern "C"
{
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
}


struct wf_cursor
{
    wf_cursor();
    ~wf_cursor();

    void attach_device(wlr_input_device *device);
    void detach_device(wlr_input_device *device);

    void set_cursor(wlr_seat_pointer_request_set_cursor_event *ev);
    void set_cursor(std::string name);

    void hide_cursor();
    void warp_cursor(int x, int y);

    void init_xcursor();

    wl_listener button, motion, motion_absolute, axis,
                swipe_begin, swipe_update, swipe_end,
                pinch_begin, pinch_update, pinch_end,
                frame;
    signal_callback_t config_reloaded;

    wlr_cursor *cursor = NULL;
    wlr_xcursor_manager *xcursor = NULL;
    int count_pressed_buttons = 0;

    wf_option mouse_scroll_speed;
    wf_option touchpad_scroll_speed;

    wayfire_surface_t *grabbed_surface = nullptr;
    void start_held_grab(wayfire_surface_t *surface);
    void end_held_grab();
};

#endif /* end of include guard: CURSOR_HPP */
