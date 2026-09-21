#pragma once

#include <filesystem>
#include <string>

namespace sakura::core
{
class Paths
{
public:
    static void SetUserRoot(std::filesystem::path path) { Root() = std::move(path); }
    static std::string User(const std::string& relative = "") { return (Root() / relative).generic_string(); }
private:
    static std::filesystem::path& Root()
    {
        static std::filesystem::path path = ".";
        return path;
    }
};
}
