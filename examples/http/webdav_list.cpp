#include <print>
#include <string>
#include <string_view>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/async/async.h"

using namespace kota;

namespace {

constexpr std::string_view webdav_password = "";
constexpr std::string_view webdav_username = "";
constexpr std::string_view webdav_url = "";
constexpr std::string_view propfind_body =
    R"(<?xml version="1.0" encoding="utf-8"?>
<d:propfind xmlns:d="DAV:">
  <d:prop>
    <d:displayname/>
    <d:resourcetype/>
    <d:getlastmodified/>
  </d:prop>
</d:propfind>
)";

task<void, http::error> list_webdav_directory(event_loop& loop) {
    http::client client;

    auto response = co_await client.on(loop)
                        .request("PROPFIND", std::string(webdav_url))
                        .basic_auth(std::string(webdav_username), std::string(webdav_password))
                        .header("depth", "1")
                        .header("content-type", "application/xml; charset=utf-8")
                        .header("accept", "application/xml, text/xml")
                        .body(std::string(propfind_body))
                        .send()
                        .or_fail();

    std::println("status: {}", response.status);
    std::println("url: {}", response.url);
    std::println();
    std::println("{}", response.text());
}

}  // namespace

int main() {
    event_loop loop;
    auto root = list_webdav_directory(loop);
    loop.schedule(root);
    loop.run();
    http::manager::unregister_loop(loop);
    return 0;
}
