#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace DewpointPath
{
inline bool inspect(
    const std::string& path,
    bool* exists,
    bool* isDirectory,
    std::string* errorMessage)
{
#ifdef _WIN32
    struct _stat status{};
    const int result = _stat(path.c_str(), &status);
#else
    struct stat status{};
    const int result = stat(path.c_str(), &status);
#endif
    if (result == 0) {
        *exists = true;
#ifdef _WIN32
        *isDirectory = (status.st_mode & _S_IFDIR) != 0;
#else
        *isDirectory = S_ISDIR(status.st_mode);
#endif
        return true;
    }

    *exists = false;
    *isDirectory = false;
    if (errno == ENOENT) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = std::strerror(errno);
    }
    return false;
}

inline bool createDirectory(const std::string& path, std::string* errorMessage)
{
#ifdef _WIN32
    const int result = _mkdir(path.c_str());
#else
    const int result = mkdir(path.c_str(), 0755);
#endif
    if (result == 0) {
        return true;
    }

    const int createError = errno;
    if (createError == EEXIST) {
        bool exists = false;
        bool isDirectory = false;
        std::string inspectError;
        if (inspect(path, &exists, &isDirectory, &inspectError) && exists && isDirectory) {
            return true;
        }
        if (!inspectError.empty()) {
            if (errorMessage) {
                *errorMessage = inspectError;
            }
            return false;
        }
    }

    if (errorMessage) {
        *errorMessage = std::strerror(createError);
    }
    return false;
}

inline std::string join(const std::string& directory, const std::string& name)
{
    if (directory.empty()) {
        return name;
    }
    const char last = directory[directory.size() - 1];
    if (last == '/' || last == '\\') {
        return directory + name;
    }
    return directory + '/' + name;
}

inline std::string normalizeForComparison(std::string path)
{
    while (path.size() > 1) {
        const char last = path[path.size() - 1];
        if (last != '/' && last != '\\') {
            break;
        }
        if (path.size() == 3 && path[1] == ':') {
            break;
        }
        path.erase(path.size() - 1);
    }
    return path;
}

inline bool same(const std::string& left, const std::string& right)
{
    return normalizeForComparison(left) == normalizeForComparison(right);
}
} // namespace DewpointPath
