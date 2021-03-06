#include "priv-view.hpp"
#include "debug.hpp"
#include "core.hpp"
#include "output.hpp"
#include "workspace-manager.hpp"

extern "C"
{
#include <wlr/config.h>

#if WLR_HAS_XWAYLAND
#define class class_t
#define static
#include <wlr/xwayland.h>
#undef static
#undef class
#endif
}

#if WLR_HAS_XWAYLAND

static void handle_xwayland_map(wl_listener* listener, void *data);
static void handle_xwayland_unmap(wl_listener*, void *data);
static void handle_xwayland_destroy(wl_listener*, void *data);
static void handle_xwayland_request_configure(wl_listener*, void *data);

class wayfire_xwayland_view_base : public wayfire_view_t
{
    protected:
    wl_listener destroy_ev, unmap_listener, map_ev, configure;

    wlr_xwayland_surface *xw;
    int last_server_width = 0;
    int last_server_height = 0;

    signal_callback_t output_geometry_changed = [this] (signal_data*)
    {
        if (is_mapped())
            move(geometry.x, geometry.y, false);
    };

    public:

    wayfire_xwayland_view_base(wlr_xwayland_surface *xww)
        : wayfire_view_t(), xw(xww)
    {
        map_ev.notify         = handle_xwayland_map;
        destroy_ev.notify     = handle_xwayland_destroy;
        unmap_listener.notify = handle_xwayland_unmap;
        configure.notify      = handle_xwayland_request_configure;

        wl_signal_add(&xw->events.destroy,            &destroy_ev);
        wl_signal_add(&xw->events.unmap,              &unmap_listener);
        wl_signal_add(&xw->events.request_configure,  &configure);
        wl_signal_add(&xw->events.map,                &map_ev);
    }

    virtual void destroy() override
    {
        if (output)
            output->disconnect_signal("output-resized", &output_geometry_changed);

        wl_list_remove(&destroy_ev.link);
        wl_list_remove(&unmap_listener.link);
        wl_list_remove(&configure.link);
        wl_list_remove(&map_ev.link);

        wayfire_view_t::destroy();
    }

    virtual void configure_request(wf_geometry configure_geometry)
    {
        if (frame)
            configure_geometry = frame->expand_wm_geometry(configure_geometry);
        set_geometry(configure_geometry);
    }

    virtual bool is_subsurface() override { return false; }
    virtual std::string get_title()  override { return nonull(xw->title);   }
    virtual std::string get_app_id() override { return nonull(xw->class_t); }

    virtual void close() override
    {
        wlr_xwayland_surface_close(xw);
        wayfire_view_t::close();
    }

    void send_configure(int width, int height)
    {
        if (width < 0 || height < 0)
        {
            /* such a configure request would freeze xwayland. This is most probably a bug */
            log_error("Configuring a xwayland surface with width/height <0");
            return;
        }

        auto output_geometry = get_output_geometry();

        int configure_x = output_geometry.x;
        int configure_y = output_geometry.y;

        if (output)
        {
            auto real_output = output->get_layout_geometry();
            configure_x += real_output.x;
            configure_y += real_output.y;
        }

        if (_is_mapped)
        {
            wlr_xwayland_surface_configure(xw,
                configure_x, configure_y, width, height);
        }
    }

    void send_configure()
    {
        send_configure(last_server_width, last_server_height);
    }

    virtual void set_output(wayfire_output *wo) override
    {
        if (output)
            output->disconnect_signal("output-resized", &output_geometry_changed);

        wayfire_view_t::set_output(wo);

        if (wo)
            wo->connect_signal("output-resized", &output_geometry_changed);

        send_configure();
    }
};

class wayfire_unmanaged_xwayland_view : public wayfire_xwayland_view_base
{
    public:
    wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww);

    int global_x, global_y;

    void commit();
    void map(wlr_surface *surface);
    void unmap();
    void activate(bool active);
    void move(int x, int y, bool s);
    void resize(int w, int h, bool s);
    void set_geometry(wf_geometry g);
    wlr_surface *get_keyboard_focus_surface();

    virtual bool should_be_decorated() { return false; }
    ~wayfire_unmanaged_xwayland_view() { }
};

static void handle_xwayland_request_move(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_move_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->move_request();
}

static void handle_xwayland_request_resize(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_resize_event*> (data);
    auto view = wf_view_from_void(ev->surface->data);
    view->resize_request(ev->edges);
}

static void handle_xwayland_request_configure(wl_listener*, void *data)
{
    auto ev = static_cast<wlr_xwayland_surface_configure_event*> (data);
    auto view = dynamic_cast<wayfire_xwayland_view_base*> (
        wf_view_from_void(ev->surface->data));

    wf_geometry geometry = {ev->x, ev->y, ev->width, ev->height};
    /* Wayfire positions views relative to their output, but Xwayland windows
     * have a global positioning. So, we need to make sure that we always
     * transform between output-local coordinates and global coordinates */
    if (view->get_output())
    {
        auto og = view->get_output()->get_layout_geometry();
        geometry.x -= og.x;
        geometry.y -= og.y;
    }

    view->configure_request(geometry);
}

static void handle_xwayland_request_maximize(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surf->data);
    view->maximize_request(surf->maximized_horz && surf->maximized_vert);
}

static void handle_xwayland_request_fullscreen(wl_listener*, void *data)
{
    auto surf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surf->data);
    view->fullscreen_request(view->get_output(), surf->fullscreen);
}

static void handle_xwayland_map(wl_listener* listener, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);
    view->map(xsurf->surface);
}

static void handle_xwayland_unmap(wl_listener*, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);
    view->unmap();
}

static void handle_xwayland_destroy(wl_listener*, void *data)
{
    auto xsurf = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(xsurf->data);
    view->destroy();
}

static void handle_xwayland_set_parent(wl_listener*, void *data)
{
    auto surface = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surface->data);
    auto parent = surface->parent ?
        wf_view_from_void(surface->parent->data)->self() : nullptr;

    assert(view);
    view->set_toplevel_parent(parent);
}

static void handle_xwayland_set_title(wl_listener *listener, void *data)
{
    auto surface = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surface->data);
    view->handle_title_changed();
}

static void handle_xwayland_set_app_id(wl_listener *listener, void *data)
{
    auto surface = static_cast<wlr_xwayland_surface*> (data);
    auto view = wf_view_from_void(surface->data);
    view->handle_app_id_changed();
}

class wayfire_xwayland_view : public wayfire_xwayland_view_base
{
    /* TODO: very bad names, also in other shells */
    wl_listener request_move, request_resize,
                request_maximize, request_fullscreen,
                set_parent_ev, set_title, set_app_id;

    public:
    wayfire_xwayland_view(wlr_xwayland_surface *xww)
        : wayfire_xwayland_view_base(xww)
    {
        log_info("new xwayland surface %s class: %s instance: %s",
                 nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

        set_title.notify          = handle_xwayland_set_title;
        set_app_id.notify         = handle_xwayland_set_app_id;
        set_parent_ev.notify      = handle_xwayland_set_parent;
        request_move.notify       = handle_xwayland_request_move;
        request_resize.notify     = handle_xwayland_request_resize;
        request_maximize.notify   = handle_xwayland_request_maximize;
        request_fullscreen.notify = handle_xwayland_request_fullscreen;

        wl_signal_add(&xw->events.set_title,          &set_title);
        wl_signal_add(&xw->events.set_class,          &set_app_id);
        wl_signal_add(&xw->events.set_parent,         &set_parent_ev);
        wl_signal_add(&xw->events.request_move,       &request_move);
        wl_signal_add(&xw->events.request_resize,     &request_resize);
        wl_signal_add(&xw->events.request_maximize,   &request_maximize);
        wl_signal_add(&xw->events.request_fullscreen, &request_fullscreen);

        xw->data = this;
    }

    virtual void destroy()
    {
        wl_list_remove(&set_parent_ev.link);
        wl_list_remove(&request_move.link);
        wl_list_remove(&request_resize.link);
        wl_list_remove(&request_maximize.link);
        wl_list_remove(&request_fullscreen.link);
        wl_list_remove(&set_title.link);
        wl_list_remove(&set_app_id.link);

        wayfire_xwayland_view_base::destroy();
    }

    void map(wlr_surface *surface)
    {
        /* override-redirect status changed between creation and MapNotify */
        if (xw->override_redirect)
        {
            auto xsurface = xw; // keep the xsurface in stack, because destroy will likely free this
            destroy();

            auto view = std::make_unique<wayfire_unmanaged_xwayland_view> (xsurface);
            auto raw = view.get();

            core->add_view(std::move(view));
            raw->map(xsurface->surface);
            return;
        }

        if (xw->maximized_horz && xw->maximized_vert)
            maximize_request(true);

        if (xw->fullscreen)
            fullscreen_request(output, true);

        if (xw->parent)
        {
            auto parent = wf_view_from_void(xw->parent->data)->self();
            set_toplevel_parent(parent);
        }

        auto real_output = output->get_layout_geometry();
        if (!maximized && !fullscreen && !parent)
            move(xw->x - real_output.x, xw->y - real_output.y, false);

        wayfire_view_t::map(surface);
        create_toplevel();
    }

    void commit()
    {
        wayfire_view_t::commit();

        /* Avoid loops where the client wants to have a certain size but the
         * compositor keeps trying to resize it */
        last_server_width = geometry.width;
        last_server_height = geometry.height;
    }

    bool is_subsurface() { return false; }

    virtual bool should_be_decorated()
    {
        return !(xw->decorations & (WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE |
                                    WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER));
    }

    void activate(bool active)
    {
        wlr_xwayland_surface_activate(xw, active);
        wayfire_view_t::activate(active);
    }

    void move(int x, int y, bool s)
    {
        wayfire_view_t::move(x, y, s);
        if (!destroyed && !in_continuous_move)
            send_configure();
    }

    void set_moving(bool moving)
    {
        wayfire_view_t::set_moving(moving);

        /* We don't send updates while in continuous move, because that means
         * too much configure requests. Instead, we set it at the end */
        if (!in_continuous_move)
            send_configure();
    }

    void resize(int w, int h, bool s)
    {
        damage();
        if (frame)
            frame->calculate_resize_size(w, h);

        last_server_width = w;
        last_server_height = h;
        send_configure(w, h);
    }

    virtual void request_native_size()
    {
        if (!_is_mapped)
            return;

        if (xw->size_hints->base_width > 0 && xw->size_hints->base_height > 0)
        {
            last_server_width = xw->size_hints->base_width;
            last_server_height = xw->size_hints->base_height;
            send_configure();
        }
    }

    /* TODO: bad with decoration */
    void set_geometry(wf_geometry g)
    {
        damage();

        wayfire_view_t::move(g.x, g.y, false);
        resize(g.width, g.height, false);
    }

    void set_maximized(bool maxim)
    {
        wayfire_view_t::set_maximized(maxim);
        wlr_xwayland_surface_set_maximized(xw, maxim);
    }

    virtual void toplevel_send_app_id()
    {
        if (!toplevel_handle)
            return;

        std::string app_id;

        auto default_app_id = get_app_id();
        auto instance_app_id = nonull(xw->instance);

        auto app_id_mode = (*core->config)["workarounds"]
            ->get_option("app_id_mode", "stock");

        if (app_id_mode->as_string() == "full") {
            app_id = default_app_id + " " + instance_app_id;
        } else {
            app_id = default_app_id;
        }

        wlr_foreign_toplevel_handle_v1_set_app_id(toplevel_handle, app_id.c_str());
    }

    void set_fullscreen(bool full)
    {
        wayfire_view_t::set_fullscreen(full);
        wlr_xwayland_surface_set_fullscreen(xw, full);
    }
};

wayfire_unmanaged_xwayland_view::wayfire_unmanaged_xwayland_view(wlr_xwayland_surface *xww)
    : wayfire_xwayland_view_base(xww)
{
    log_info("new unmanaged xwayland surface %s class: %s instance: %s",
             nonull(xw->title), nonull(xw->class_t), nonull(xw->instance));

    xw->data = this;
    role = WF_VIEW_ROLE_UNMANAGED;
}

void wayfire_unmanaged_xwayland_view::commit()
{
    if (global_x != xw->x || global_y != xw->y)
    {
        geometry.x = global_x = xw->x;
        geometry.y = global_y = xw->y;

        if (output)
        {
            auto real_output = output->get_layout_geometry();
            geometry.x -= real_output.x;
            geometry.y -= real_output.y;
        }

        wayfire_view_t::move(geometry.x, geometry.y, false);
    }

    wayfire_surface_t::commit();

    auto old_geometry = geometry;
    if (update_size())
    {
        damage(old_geometry);
        damage();
    }
}

void wayfire_unmanaged_xwayland_view::map(wlr_surface *surface)
{
    _is_mapped = true;
    /* move to the output where our center is
     * FIXME: this is a bad idea, because a dropdown menu might get sent to
     * an incorrect output. However, no matter how we calculate the real
     * output, we just can't be 100% compatible because in X all windows are
     * positioned in a global coordinate space */
    auto wo = core->get_output_at(xw->x + surface->current.width / 2, xw->y + surface->current.height / 2);

    if (!wo)
    {
        /* if surface center is outside of anything, try to check the output where the pointer is */
        GetTuple(cx, cy, core->get_cursor_position());
        wo = core->get_output_at(cx, cy);
    }

    if (!wo)
        wo = core->get_active_output();
    assert(wo);


    auto real_output_geometry = wo->get_layout_geometry();

    global_x = xw->x;
    global_y = xw->y;
    wayfire_view_t::move(xw->x - real_output_geometry.x,
        xw->y - real_output_geometry.y, false);

    if (wo != output)
    {
        if (output)
            output->workspace->add_view_to_layer(self(), 0);

        set_output(wo);
    }

    damage();

    wayfire_surface_t::map(surface);
    /* We update the keyboard focus before emitting the map event, so that
     * plugins can detect that this view can have keyboard focus */
    _keyboard_focus_enabled = wlr_xwayland_or_surface_wants_focus(xw);

    output->workspace->add_view_to_layer(self(), WF_LAYER_XWAYLAND);
    emit_view_map(self());
    if (wlr_xwayland_or_surface_wants_focus(xw))
    {
        auto wa = output->workspace->get_workarea();
        move(xw->x + wa.x - real_output_geometry.x,
            xw->y + wa.y - real_output_geometry.y, false);

        output->focus_view(self());
    }
}

void wayfire_unmanaged_xwayland_view::unmap()
{
    _is_mapped = false;
    emit_view_unmap(self());
    wayfire_surface_t::unmap();
}

void wayfire_unmanaged_xwayland_view::activate(bool active)
{
    wayfire_view_t::activate(active);
    wlr_xwayland_surface_activate(xw, active);
}

void wayfire_unmanaged_xwayland_view::move(int x, int y, bool s)
{
    damage();
    geometry.x = x;
    geometry.y = y;
    send_configure();
}

void wayfire_unmanaged_xwayland_view::resize(int w, int h, bool s)
{
    damage();
    geometry.width = w;
    geometry.height = h;
    send_configure();
}

void wayfire_unmanaged_xwayland_view::set_geometry(wf_geometry g)
{
    damage();
    geometry = g;
    send_configure();
}

wlr_surface *wayfire_unmanaged_xwayland_view::get_keyboard_focus_surface()
{
    if (wlr_xwayland_or_surface_wants_focus(xw))
        return wayfire_view_t::get_keyboard_focus_surface();

    return nullptr;
}

void notify_xwayland_created(wl_listener *, void *data)
{
    auto xsurf = (wlr_xwayland_surface*) data;

    if (xsurf->override_redirect)
    {
        core->add_view(std::make_unique<wayfire_unmanaged_xwayland_view>(xsurf));
    } else
    {
        core->add_view(std::make_unique<wayfire_xwayland_view> (xsurf));
    }
}

static wl_listener xwayland_created;
static wlr_xwayland *xwayland_handle = nullptr;
#endif

void init_xwayland()
{
#if WLR_HAS_XWAYLAND
    xwayland_created.notify = notify_xwayland_created;
    xwayland_handle = wlr_xwayland_create(core->display, core->compositor, false);

    if (xwayland_handle)
        wl_signal_add(&xwayland_handle->events.new_surface, &xwayland_created);
#endif
}

void xwayland_set_seat(wlr_seat *seat)
{
#if WLR_HAS_XWAYLAND
    if (xwayland_handle)
        wlr_xwayland_set_seat(xwayland_handle, core->get_current_seat());
#endif
}

std::string xwayland_get_display()
{
#if WLR_HAS_XWAYLAND
    return std::to_string(xwayland_handle ? xwayland_handle->display : -1);
#else
    return "-1";
#endif
}
