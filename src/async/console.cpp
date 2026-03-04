#include <memory>

#include "awaiter.h"
#include "eventide/async/loop.h"

namespace eventide {

result<console> console::open(int fd, console::options opts, event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::tty_init(loop, self->tty, fd, opts.readable)) {
        return std::unexpected(err);
    }

    return console(std::move(self));
}

error console::set_mode(mode value) {
    if(!self || !self->initialized()) {
        return error::invalid_argument;
    }

    uv_tty_mode_t uv_mode = UV_TTY_MODE_NORMAL;
    switch(value) {
        case mode::normal: uv_mode = UV_TTY_MODE_NORMAL; break;
        case mode::raw: uv_mode = UV_TTY_MODE_RAW; break;
        case mode::io: uv_mode = UV_TTY_MODE_IO; break;
        case mode::raw_vt: uv_mode = UV_TTY_MODE_RAW_VT; break;
    }

    if(auto err = uv::tty_set_mode(self->tty, uv_mode)) {
        return err;
    }

    return {};
}

error console::reset_mode() {
    if(auto err = uv::tty_reset_mode()) {
        return err;
    }
    return {};
}

result<console::winsize> console::get_winsize() const {
    if(!self || !self->initialized()) {
        return std::unexpected(error::invalid_argument);
    }

    auto out = uv::tty_get_winsize(self->tty);
    if(!out) {
        return std::unexpected(out.error());
    }

    return winsize{out->width, out->height};
}

void console::set_vterm_state(vterm_state state) {
    auto uv_state = state == vterm_state::supported ? UV_TTY_SUPPORTED : UV_TTY_UNSUPPORTED;
    uv::tty_set_vterm_state(uv_state);
}

result<console::vterm_state> console::get_vterm_state() {
    auto out = uv::tty_get_vterm_state();
    if(!out) {
        return std::unexpected(out.error());
    }

    return *out == UV_TTY_SUPPORTED ? vterm_state::supported : vterm_state::unsupported;
}

console::console(unique_handle<Self> self) noexcept : stream(std::move(self)) {}

}  // namespace eventide
