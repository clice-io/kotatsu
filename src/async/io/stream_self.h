#pragma once

#include "awaiter.h"
#include "ring_buffer.h"
#include "kota/async/io/stream.h"

namespace kota {

struct stream::Self : uv::owned_handle<Self> {
    union {
        uv_handle_t handle;
        uv_stream_t stream;
        uv_pipe_t pipe;
        uv_tcp_t tcp;
        uv_tty_t tty;
    };

    /// The pending read.
    uv::waiter_slot<void> slot;

    ring_buffer buffer;

    /// What ended reading, the end of the stream included; reported once
    /// the buffer is drained, and for good.
    error ended;

    bool reading = false;

    /// Resumes once the buffer holds bytes: at once if it does, with what
    /// ended reading once it is drained, or with the error starting to read
    /// ran into; otherwise it reads ahead and waits.
    uv::waiter_slot<void>::awaiter fill();

    void stop_reading();

    static void on_alloc(uv_handle_t* handle, std::size_t, uv_buf_t* buf);

    static void on_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t*);
};

}  // namespace kota
