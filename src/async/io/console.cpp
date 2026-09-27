#include <utility>

#include "stream_self.h"

namespace kota {

console::console(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

result<console> console::open(int fd, event_loop& loop) {
    return open(fd, options{}, loop);
}

result<console> console::open(int fd, options opts, event_loop& loop) {
    auto self = Self::make();
    if(auto err = error(::uv_tty_init(loop.native_handle(), &self->tty, fd, opts.readable))) {
        return outcome_error(err);
    }
    return console(std::move(self));
}

error console::set_mode(mode value) {
    if(!self) {
        return error::invalid_argument;
    }

    uv_tty_mode_t uv_mode = UV_TTY_MODE_NORMAL;
    switch(value) {
        case mode::normal: uv_mode = UV_TTY_MODE_NORMAL; break;
        case mode::raw: uv_mode = UV_TTY_MODE_RAW; break;
        case mode::io: uv_mode = UV_TTY_MODE_IO; break;
        case mode::raw_vt: uv_mode = UV_TTY_MODE_RAW_VT; break;
    }
    return error(::uv_tty_set_mode(&self->tty, uv_mode));
}

error console::reset_mode() {
    return error(::uv_tty_reset_mode());
}

result<console::winsize> console::get_winsize() const {
    if(!self) {
        return outcome_error(error::invalid_argument);
    }

    winsize out;
    if(auto err = error(::uv_tty_get_winsize(&self->tty, &out.width, &out.height))) {
        return outcome_error(err);
    }
    return out;
}

void console::set_vterm_state(vterm_state state) {
    ::uv_tty_set_vterm_state(state == vterm_state::supported ? UV_TTY_SUPPORTED
                                                             : UV_TTY_UNSUPPORTED);
}

result<console::vterm_state> console::get_vterm_state() {
    uv_tty_vtermstate_t state;
    if(auto err = error(::uv_tty_get_vterm_state(&state))) {
        return outcome_error(err);
    }
    return state == UV_TTY_SUPPORTED ? vterm_state::supported : vterm_state::unsupported;
}

}  // namespace kota
