#include "kotatsu/fs/temp.h"

#include <utility>

#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/resolve.h"

namespace kotatsu::fs {

std::expected<TempDirectory, std::error_code> TempDirectory::create(std::string_view directory,
                                                                    std::string_view prefix) {
    std::string made;
    if(auto error = create_temp_directory(made, directory, prefix)) {
        return std::unexpected(error);
    }
    TempDirectory temp(std::move(made));
    // Apart from `location`, which a failure would leave unspecified for the
    // destructor to remove.
    std::string real;
    if(auto error = canonical(real, temp.location)) {
        return std::unexpected(error);
    }
    temp.location = std::move(real);
    return temp;
}

TempDirectory::TempDirectory(TempDirectory&& other) noexcept :
    location(std::exchange(other.location, {})) {}

TempDirectory& TempDirectory::operator=(TempDirectory&& other) noexcept {
    if(this != &other) {
        [[maybe_unused]] auto error = remove();
        location = std::exchange(other.location, {});
    }
    return *this;
}

TempDirectory::~TempDirectory() {
    [[maybe_unused]] auto error = remove();
}

std::error_code TempDirectory::remove() {
    if(location.empty()) {
        return {};
    }
    auto removed = remove_all(location);
    if(!removed) {
        return removed.error();
    }
    location.clear();
    return {};
}

std::string TempDirectory::keep() && {
    return std::exchange(location, {});
}

std::expected<TempFile, std::error_code> TempFile::create(std::string_view directory,
                                                          std::string_view prefix,
                                                          std::string_view suffix) {
    std::string made;
    auto file = create_temp_file(made, directory, prefix, suffix);
    if(!file) {
        return std::unexpected(file.error());
    }
    TempFile temp(std::move(made), std::move(*file));
    // Apart from `location`, as TempDirectory::create() does.
    std::string real;
    if(auto error = canonical(real, temp.location)) {
        return std::unexpected(error);
    }
    temp.location = std::move(real);
    return temp;
}

TempFile::TempFile(TempFile&& other) noexcept :
    location(std::exchange(other.location, {})), opened(std::move(other.opened)) {}

TempFile& TempFile::operator=(TempFile&& other) noexcept {
    if(this != &other) {
        [[maybe_unused]] auto error = remove();
        // One kept is still open, which a swap would hand to `other`.
        [[maybe_unused]] auto closed = opened.close();
        location = std::exchange(other.location, {});
        opened = std::move(other.opened);
    }
    return *this;
}

TempFile::~TempFile() {
    [[maybe_unused]] auto error = remove();
}

std::error_code TempFile::remove() {
    if(location.empty()) {
        return {};
    }
    // Closed first: where Windows deletes the classic way (FAT), the name
    // stays until the last handle goes.
    [[maybe_unused]] auto closed = opened.close();
    auto removed = fs::remove(location);
    if(!removed) {
        return removed.error();
    }
    location.clear();
    return {};
}

std::string TempFile::keep() && {
    return std::exchange(location, {});
}

}  // namespace kotatsu::fs
