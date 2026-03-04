#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "error.h"
#include "owned.h"
#include "task.h"

namespace eventide {

class event_loop;

class timer {
public:
    timer() noexcept;

    timer(const timer&) = delete;
    timer& operator=(const timer&) = delete;

    timer(timer&& other) noexcept;
    timer& operator=(timer&& other) noexcept;

    ~timer();

    struct Self;
    Self* operator->() noexcept;

    static timer create(event_loop& loop = event_loop::current());

    void start(std::chrono::milliseconds timeout, std::chrono::milliseconds repeat = {});

    void stop();

    task<> wait();

private:
    explicit timer(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class signal {
public:
    signal() noexcept;

    signal(const signal&) = delete;
    signal& operator=(const signal&) = delete;

    signal(signal&& other) noexcept;
    signal& operator=(signal&& other) noexcept;

    ~signal();

    struct Self;
    Self* operator->() noexcept;

    static result<signal> create(event_loop& loop = event_loop::current());

    error start(int signum);

    error stop();

    task<error> wait();

private:
    explicit signal(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class idle {
public:
    idle() noexcept;

    idle(const idle&) = delete;
    idle& operator=(const idle&) = delete;

    idle(idle&& other) noexcept;
    idle& operator=(idle&& other) noexcept;

    ~idle();

    struct Self;
    Self* operator->() noexcept;

    static idle create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit idle(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class prepare {
public:
    prepare() noexcept;

    prepare(const prepare&) = delete;
    prepare& operator=(const prepare&) = delete;

    prepare(prepare&& other) noexcept;
    prepare& operator=(prepare&& other) noexcept;

    ~prepare();

    struct Self;
    Self* operator->() noexcept;

    static prepare create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit prepare(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

class check {
public:
    check() noexcept;

    check(const check&) = delete;
    check& operator=(const check&) = delete;

    check(check&& other) noexcept;
    check& operator=(check&& other) noexcept;

    ~check();

    struct Self;
    Self* operator->() noexcept;

    static check create(event_loop& loop = event_loop::current());

    void start();

    void stop();

    task<> wait();

private:
    explicit check(unique_handle<Self> self) noexcept;

    unique_handle<Self> self;
};

task<> sleep(std::chrono::milliseconds timeout, event_loop& loop = event_loop::current());

}  // namespace eventide
