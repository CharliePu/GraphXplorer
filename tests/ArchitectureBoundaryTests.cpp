#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::filesystem::path repoRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src"))
        {
            return current;
        }
        current = current.parent_path();
    }
    throw std::runtime_error("Could not locate repository root");
}

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

std::vector<std::filesystem::path> sourceFiles(const std::filesystem::path &dir)
{
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext == ".h" || ext == ".cpp")
        {
            files.push_back(entry.path());
        }
    }
    return files;
}

void requireNoInclude(const std::filesystem::path &dir, const std::vector<std::string> &forbidden)
{
    for (const auto &file : sourceFiles(dir))
    {
        const auto text = readFile(file);
        for (const auto &pattern : forbidden)
        {
            INFO("file=" << file.string() << " pattern=" << pattern);
            CHECK(text.find(pattern) == std::string::npos);
        }
    }
}
}

TEST_CASE("Architecture boundary cut forbids core subsystem back edges", "[Architecture]")
{
    const auto root = repoRoot();

    requireNoInclude(root / "src" / "Render", {"../UI/", "/UI/"});
    requireNoInclude(root / "src" / "Compute", {"../Render/", "/Render/", "../UI/", "/UI/"});
    requireNoInclude(root / "src" / "Formula", {"../Tile/", "../Compute/", "../Render/", "../UI/", "../App/"});
}
