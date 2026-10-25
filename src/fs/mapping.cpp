#include "kota/fs/mapping.h"

#include <utility>

#ifdef _WIN32
#include "win32.h"
#else
#include <sys/mman.h>

#include "posix.h"
#endif

namespace kota::fs {

Mapping::Mapping(Mapping&& other) noexcept :
    data(std::exchange(other.data, nullptr)), size(std::exchange(other.size, 0)) {}

Mapping& Mapping::operator=(Mapping&& other) noexcept {
    std::swap(data, other.data);
    std::swap(size, other.size);
    return *this;
}

Mapping::~Mapping() {
    if(data == nullptr) {
        return;
    }
#ifdef _WIN32
    ::UnmapViewOfFile(data);
#else
    ::munmap(const_cast<char*>(data), size);
#endif
}

std::expected<Mapping, std::error_code> Mapping::map(const File& file) {
    auto status = file.status();
    if(!status) {
        return std::unexpected(status.error());
    }
    // Neither system maps nothing.
    if(status->size == 0) {
        return Mapping();
    }
    auto size = static_cast<std::size_t>(status->size);
#ifdef _WIN32
    // At the size asked for, a file cut short since fails here rather than
    // fault where the view would pass its end.
    LARGE_INTEGER bound;
    bound.QuadPart = static_cast<LONGLONG>(size);
    HANDLE section = ::CreateFileMappingW(file.native_handle(),
                                          nullptr,
                                          PAGE_READONLY,
                                          static_cast<DWORD>(bound.HighPart),
                                          bound.LowPart,
                                          nullptr);
    if(section == nullptr) {
        return std::unexpected(windows::last_error());
    }
    // The view keeps the section it maps.
    void* view = ::MapViewOfFile(section, FILE_MAP_READ, 0, 0, size);
    auto error = view == nullptr ? windows::last_error() : std::error_code();
    ::CloseHandle(section);
    if(error) {
        return std::unexpected(error);
    }
#else
    void* view = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, file.native_handle(), 0);
    if(view == MAP_FAILED) {
        return std::unexpected(posix::last_error());
    }
#endif
    return Mapping(static_cast<const char*>(view), size);
}

}  // namespace kota::fs
