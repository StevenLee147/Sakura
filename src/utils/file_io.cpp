#include "file_io.h"

#include <fstream>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace sakura::utils
{
bool AtomicWrite(const std::filesystem::path& path, std::string_view contents)
{
    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream.good()) return false;
    }
#ifdef _WIN32
    const bool success = MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, ec);
    const bool success = !ec;
#endif
    if (!success) std::filesystem::remove(temporary, ec);
    return success;
}
}
