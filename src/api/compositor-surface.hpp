#ifndef COMPOSITOR_SURFACE_HPP
#define COMPOSITOR_SURFACE_HPP

#include "view.hpp"

/* The base class for a compositor surface, it just supports cursor and touch input */
class wayfire_compositor_surface_t
{
    public:
    virtual ~wayfire_compositor_surface_t() {}

    virtual void on_pointer_enter(int x, int y) {}
    virtual void on_pointer_leave() {}
    virtual void on_pointer_motion(int x, int y) {}
    virtual void on_pointer_button(uint32_t button, uint32_t state) {}

    virtual void on_touch_down(int x, int y) {}
    virtual void on_touch_up() {}
    virtual void on_touch_motion(int x, int y) {}
};

wayfire_compositor_surface_t *wf_compositor_surface_from_surface(wayfire_surface_t *surface);

class wayfire_compositor_subsurface_t : public wayfire_surface_t, public wayfire_compositor_surface_t
{
    protected:
        virtual void damage(const wlr_box& box) { assert(false); }
        virtual void _wlr_render_box(const wf_framebuffer& fb, int x, int y, const wlr_box& scissor) { assert(false); }

    public:
        wayfire_compositor_subsurface_t() {}
        virtual ~wayfire_compositor_subsurface_t() {}

        virtual bool is_mapped() { return true; }

        virtual wf_geometry get_output_geometry() { assert(false); return {}; }
        virtual void  render_fb(const wf_region& damage, const wf_framebuffer& fb) { assert(false); }

        virtual void send_frame_done(const timespec& now) {}
        virtual void subtract_opaque(wf_region& region, int x, int y) {}

        /* all input events coordinates are surface-local */

        /* override this if you want to get pointer events or to stop input passthrough */
        virtual bool accepts_input(int32_t sx, int32_t sy) { return false; }

};

void emit_map_state_change(wayfire_surface_t *surface);

#endif /* end of include guard: COMPOSITOR_SURFACE_HPP */
