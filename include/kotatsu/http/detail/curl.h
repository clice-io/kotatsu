#pragma once

#include <cassert>
#include <curl/curl.h>
#include <string_view>
#include <utility>

namespace kotatsu::curl {

using easy_error = CURLcode;
using multi_error = CURLMcode;
using share_error = CURLSHcode;

inline bool ok(easy_error code) noexcept {
    return code == CURLE_OK;
}

inline bool ok(multi_error code) noexcept {
    return code == CURLM_OK;
}

inline bool ok(share_error code) noexcept {
    return code == CURLSHE_OK;
}

inline easy_error to_easy_error(multi_error code) noexcept {
    return ok(code) ? CURLE_OK : CURLE_FAILED_INIT;
}

inline easy_error to_easy_error(share_error code) noexcept {
    return ok(code) ? CURLE_OK : CURLE_FAILED_INIT;
}

inline std::string_view message(easy_error code) noexcept {
    auto* text = ::curl_easy_strerror(code);
    return text ? std::string_view(text) : std::string_view();
}

inline std::string_view message(multi_error code) noexcept {
    auto* text = ::curl_multi_strerror(code);
    return text ? std::string_view(text) : std::string_view();
}

inline std::string_view message(share_error code) noexcept {
    auto* text = ::curl_share_strerror(code);
    return text ? std::string_view(text) : std::string_view();
}

inline easy_error global_init(long flags = CURL_GLOBAL_DEFAULT) noexcept {
    return ::curl_global_init(flags);
}

inline void global_cleanup() noexcept {
    ::curl_global_cleanup();
}

inline CURL* easy_init() noexcept {
    return ::curl_easy_init();
}

inline void easy_cleanup(CURL* handle) noexcept {
    if(handle) {
        ::curl_easy_cleanup(handle);
    }
}

inline CURLM* multi_init() noexcept {
    return ::curl_multi_init();
}

inline CURLSH* share_init() noexcept {
    return ::curl_share_init();
}

inline void multi_cleanup(CURLM* handle) noexcept {
    if(handle) {
        ::curl_multi_cleanup(handle);
    }
}

inline void share_cleanup(CURLSH* handle) noexcept {
    if(handle) {
        ::curl_share_cleanup(handle);
    }
}

inline void slist_free_all(curl_slist* list) noexcept {
    if(list) {
        ::curl_slist_free_all(list);
    }
}

template <typename T>
inline easy_error setopt(CURL* handle, CURLoption option, T value) noexcept {
    assert(handle != nullptr && "curl::setopt requires non-null easy handle");
    return static_cast<easy_error>(::curl_easy_setopt(handle, option, value));
}

template <typename T>
inline easy_error getinfo(CURL* handle, CURLINFO info, T value) noexcept {
    assert(handle != nullptr && "curl::getinfo requires non-null easy handle");
    return static_cast<easy_error>(::curl_easy_getinfo(handle, info, value));
}

template <typename T>
inline multi_error multi_setopt(CURLM* handle, CURLMoption option, T value) noexcept {
    assert(handle != nullptr && "curl::multi_setopt requires non-null multi handle");
    return static_cast<multi_error>(::curl_multi_setopt(handle, option, value));
}

template <typename T>
inline share_error share_setopt(CURLSH* handle, CURLSHoption option, T value) noexcept {
    assert(handle != nullptr && "curl::share_setopt requires non-null share handle");
    return static_cast<share_error>(::curl_share_setopt(handle, option, value));
}

inline multi_error multi_add_handle(CURLM* multi, CURL* easy) noexcept {
    assert(multi != nullptr && "curl::multi_add_handle requires non-null multi handle");
    assert(easy != nullptr && "curl::multi_add_handle requires non-null easy handle");
    return static_cast<multi_error>(::curl_multi_add_handle(multi, easy));
}

inline multi_error multi_remove_handle(CURLM* multi, CURL* easy) noexcept {
    assert(multi != nullptr && "curl::multi_remove_handle requires non-null multi handle");
    assert(easy != nullptr && "curl::multi_remove_handle requires non-null easy handle");
    return static_cast<multi_error>(::curl_multi_remove_handle(multi, easy));
}

inline multi_error multi_assign(CURLM* multi, curl_socket_t socket, void* ptr) noexcept {
    assert(multi != nullptr && "curl::multi_assign requires non-null multi handle");
    return static_cast<multi_error>(::curl_multi_assign(multi, socket, ptr));
}

inline multi_error multi_socket_action(CURLM* multi,
                                       curl_socket_t socket,
                                       int events,
                                       int* running_handles) noexcept {
    assert(multi != nullptr && "curl::multi_socket_action requires non-null multi handle");
    assert(running_handles != nullptr && "curl::multi_socket_action requires running counter");
    return static_cast<multi_error>(
        ::curl_multi_socket_action(multi, socket, events, running_handles));
}

inline CURLMsg* multi_info_read(CURLM* multi, int* msgs_in_queue) noexcept {
    assert(multi != nullptr && "curl::multi_info_read requires non-null multi handle");
    assert(msgs_in_queue != nullptr && "curl::multi_info_read requires queue counter");
    return ::curl_multi_info_read(multi, msgs_in_queue);
}

inline curl_slist* slist_append(curl_slist* list, const char* text) noexcept {
    assert(text != nullptr && "curl::slist_append requires non-null text");
    return ::curl_slist_append(list, text);
}

class easy_handle {
public:
    easy_handle() noexcept = default;

    explicit easy_handle(CURL* handle) noexcept : handle(handle) {}

    ~easy_handle() {
        reset();
    }

    easy_handle(const easy_handle&) = delete;
    easy_handle& operator=(const easy_handle&) = delete;

    easy_handle(easy_handle&& other) noexcept : handle(other.release()) {}

    easy_handle& operator=(easy_handle&& other) noexcept {
        if(this != &other) {
            reset(other.release());
        }
        return *this;
    }

    static easy_handle create() noexcept {
        return easy_handle(easy_init());
    }

    CURL* get() const noexcept {
        return handle;
    }

    explicit operator bool() const noexcept {
        return handle != nullptr;
    }

    CURL* release() noexcept {
        return std::exchange(handle, nullptr);
    }

    void reset(CURL* next = nullptr) noexcept {
        easy_cleanup(handle);
        handle = next;
    }

private:
    CURL* handle = nullptr;
};

class multi_handle {
public:
    multi_handle() noexcept = default;

    explicit multi_handle(CURLM* handle) noexcept : handle(handle) {}

    ~multi_handle() {
        reset();
    }

    multi_handle(const multi_handle&) = delete;
    multi_handle& operator=(const multi_handle&) = delete;

    multi_handle(multi_handle&& other) noexcept : handle(other.release()) {}

    multi_handle& operator=(multi_handle&& other) noexcept {
        if(this != &other) {
            reset(other.release());
        }
        return *this;
    }

    static multi_handle create() noexcept {
        return multi_handle(multi_init());
    }

    CURLM* get() const noexcept {
        return handle;
    }

    explicit operator bool() const noexcept {
        return handle != nullptr;
    }

    CURLM* release() noexcept {
        return std::exchange(handle, nullptr);
    }

    void reset(CURLM* next = nullptr) noexcept {
        multi_cleanup(handle);
        handle = next;
    }

private:
    CURLM* handle = nullptr;
};

class share_handle {
public:
    share_handle() noexcept = default;

    explicit share_handle(CURLSH* handle) noexcept : handle(handle) {}

    ~share_handle() {
        reset();
    }

    share_handle(const share_handle&) = delete;
    share_handle& operator=(const share_handle&) = delete;

    share_handle(share_handle&& other) noexcept : handle(other.release()) {}

    share_handle& operator=(share_handle&& other) noexcept {
        if(this != &other) {
            reset(other.release());
        }
        return *this;
    }

    static share_handle create() noexcept {
        return share_handle(share_init());
    }

    CURLSH* get() const noexcept {
        return handle;
    }

    explicit operator bool() const noexcept {
        return handle != nullptr;
    }

    CURLSH* release() noexcept {
        return std::exchange(handle, nullptr);
    }

    void reset(CURLSH* next = nullptr) noexcept {
        share_cleanup(handle);
        handle = next;
    }

private:
    CURLSH* handle = nullptr;
};

class slist {
public:
    slist() noexcept = default;

    explicit slist(curl_slist* list) noexcept : head(list) {}

    ~slist() {
        reset();
    }

    slist(const slist&) = delete;
    slist& operator=(const slist&) = delete;

    slist(slist&& other) noexcept : head(other.release()) {}

    slist& operator=(slist&& other) noexcept {
        if(this != &other) {
            reset(other.release());
        }
        return *this;
    }

    curl_slist* get() const noexcept {
        return head;
    }

    explicit operator bool() const noexcept {
        return head != nullptr;
    }

    curl_slist* release() noexcept {
        return std::exchange(head, nullptr);
    }

    void reset(curl_slist* next = nullptr) noexcept {
        slist_free_all(head);
        head = next;
    }

    bool append(const char* text) noexcept {
        auto* next = slist_append(head, text);
        if(next == nullptr) {
            return false;
        }
        head = next;
        return true;
    }

private:
    curl_slist* head = nullptr;
};

}  // namespace kotatsu::curl
