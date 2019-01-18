#include <cstdlib>
#include <cstring>
#include <getopt.h>

#include <sys/inotify.h>
#include <unistd.h>

#include "debug-func.hpp"
#include <config.hpp>
#include "main.hpp"

extern "C"
{
#include <wlr/render/gles2.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/util/log.h>
}

#include <wayland-server.h>
#include "view/priv-view.hpp"

#include "core.hpp"
#include "output.hpp"

wf_runtime_config runtime_config;

static wl_listener output_created;
void output_created_cb (wl_listener*, void *data)
{
    core->add_output((wlr_output*) data);
}

void compositor_wake_cb (wl_listener*, void*)
{
}

void compositor_sleep_cb (wl_listener*, void*)
{
}

#define INOT_BUF_SIZE (1024 * sizeof(inotify_event))
char buf[INOT_BUF_SIZE];

static std::string config_file;
static void reload_config(int fd)
{
    core->config->reload_config();
    inotify_add_watch(fd, config_file.c_str(), IN_MODIFY);
}

static int handle_config_updated(int fd, uint32_t mask, void *data)
{
    log_info("got a reload");

    /* read, but don't use */
    read(fd, buf, INOT_BUF_SIZE);
    reload_config(fd);

    core->emit_signal("reload-config", nullptr);
    return 1;
}

std::map<EGLint, EGLint> default_attribs = {
    {EGL_RED_SIZE, 1},
    {EGL_GREEN_SIZE, 1},
    {EGL_BLUE_SIZE, 1},
    {EGL_DEPTH_SIZE, 1},
};

std::map<wlr_renderer*, wlr_egl*> egl_for_renderer;

/* Merge the default config and the config we need */
static std::vector<EGLint> generate_config_attribs(EGLint *renderer_attribs)
{
    std::vector<EGLint> attribs;

    /* See what we have in the default config */
    for (auto i = renderer_attribs; i != NULL && *i != EGL_NONE; i++)
    {
        /* We will override this value later */
        if (default_attribs.count(*i))
        {
            ++i;
            continue;
        }

        attribs.push_back(*i);
        i++;
        attribs.push_back(*i);
    }

    /* Then pack all values we want */
    for (auto &p : default_attribs)
    {
        attribs.push_back(p.first);
        attribs.push_back(p.second);
    }

    attribs.push_back(EGL_NONE);
    return attribs;
}

wlr_renderer *add_egl_depth_renderer(wlr_egl *egl, EGLenum platform,
                                     void *remote, EGLint *_r_attr, EGLint visual)
{
    bool r;
    auto attribs = generate_config_attribs(_r_attr);
    r = wlr_egl_init(egl, platform, remote, attribs.data(), visual);

    if (!r)
    {
        log_error ("Failed to initialize EGL");
        return NULL;
    }

    auto renderer = wlr_gles2_renderer_create(egl);
    if (!renderer)
    {
        log_error ("Failed to create GLES2 renderer");
        wlr_egl_finish(egl);
        return NULL;
    }

    egl_for_renderer[renderer] = egl;
    return renderer;
}

int main(int argc, char *argv[])
{
    /*
    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);
    */

#ifdef WAYFIRE_DEBUG_ENABLED
    wlr_log_init(WLR_DEBUG, NULL);
#else
    wlr_log_init(WLR_ERROR, NULL);
#endif

    std::string home_dir = secure_getenv("HOME");
    config_file = home_dir + "/.config/wayfire.ini";

    struct option opts[] = {
        { "config",          required_argument, NULL, 'c' },
        { "damage-debug",    no_argument,       NULL, 'd' },
        { "damage-rerender", no_argument,       NULL, 'R' },
        { 0,                 0,                 NULL,  0  }
    };

    int c, i;
    while((c = getopt_long(argc, argv, "c:dR", opts, &i)) != -1)
    {
        switch(c)
        {
            case 'c':
                config_file = optarg;
                break;
            case 'd':
                runtime_config.damage_debug = true;
                break;
            case 'R':
                runtime_config.no_damage_track = true;
                break;
            default:
                log_error("unrecognized command line argument %s", optarg);
        }
    }

    log_info("Starting wayfire");

    core = new wayfire_core();
    core->display  = wl_display_create();
    core->ev_loop  = wl_display_get_event_loop(core->display);
    core->backend  = wlr_backend_autocreate(core->display, add_egl_depth_renderer);
    core->renderer = wlr_backend_get_renderer(core->backend);
    core->egl = egl_for_renderer[core->renderer];
    assert(core->egl);

    log_info("using config file: %s", config_file.c_str());
    core->config = new wayfire_config(config_file);

    int inotify_fd = inotify_init();
    reload_config(inotify_fd);

    wl_event_loop_add_fd(core->ev_loop, inotify_fd, WL_EVENT_READABLE, handle_config_updated, NULL);

    /*
    ec->idle_time = config->get_section("core")->get_int("idle_time", 300);
    */
    core->init(core->config);

    auto server_name = wl_display_add_socket_auto(core->display);
    if (!server_name)
    {
        log_error("failed to create wayland, socket, exiting");
        return -1;
    }

    setenv("_WAYLAND_DISPLAY", server_name, 1);

    core->wayland_display = server_name;

    output_created.notify = output_created_cb;
    wl_signal_add(&core->backend->events.new_output, &output_created);

    if (!wlr_backend_start(core->backend))
    {
        log_error("failed to initialize backend, exiting");
        wlr_backend_destroy(core->backend);
        wl_display_destroy(core->display);

        return -1;
    }

    log_info ("running at server %s", server_name);
    setenv("WAYLAND_DISPLAY", server_name, 1);

    xwayland_set_seat(core->get_current_seat());
    core->wake();

    wl_display_run(core->display);

    wlr_backend_destroy(core->backend);
    wl_display_destroy(core->display);

    return EXIT_SUCCESS;
}
