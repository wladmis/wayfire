#ifndef VIEW_CHANGE_VIEWPORT_CPP
#define VIEW_CHANGE_VIEWPORT_CPP

#include <signal-definitions.hpp>

struct view_change_viewport_signal : public _view_signal
{
    std::tuple<int, int> from, to;
};

#endif /* end of include guard: VIEW_CHANGE_VIEWPORT_CPP */
