#include <filesystem>
#include <fstream>
#include <print>
#include <string>

#include "kota/http/detail/manager.h"
#include "kota/http/http.h"
#include "kota/async/async.h"

using namespace kota;

namespace {

task<void, http::error> download_avatar(event_loop& loop) {
    http::client client(loop);

    auto response = co_await client.get("https://avatars.githubusercontent.com/u/75871375?v=4")
                        .header("accept", "image/*")
                        .send()
                        .or_fail();

    auto image_path = std::filesystem::path("myavatar.png");
    std::println("Save in {}", std::filesystem::absolute(image_path).string());
    std::ofstream out(image_path, std::ios::binary);
    auto bytes = response.bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if(auto content_type = response.header_value("content-type")) {
        std::println("content-type: {}", *content_type);
    }
}

}  // namespace

int main() {
    event_loop loop;
    auto root = download_avatar(loop);
    loop.schedule(root);
    loop.run();
    http::manager::unregister_loop(loop);
    return 0;
}
