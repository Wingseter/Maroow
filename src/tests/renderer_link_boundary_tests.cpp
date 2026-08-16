#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool verify_binary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "Renderer link boundary could not open " << path << ".\n";
        return false;
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    const std::string searchable = lowercase(bytes);

    std::vector<std::string_view> forbidden{
        "sapp_",
        "sglue_",
        "sokol_app",
        "sokol_glue",
    };
#if defined(__APPLE__)
    forbidden.insert(
        forbidden.end(),
        {"cocoa.framework", "metal.framework", "quartzcore.framework",
         "opengl.framework"});
#elif defined(_WIN32)
    forbidden.push_back("opengl32.dll");
#else
    forbidden.insert(
        forbidden.end(),
        {"libgl.so", "libx11.so", "libxi.so", "libxcursor.so"});
#endif

    for (const std::string_view token : forbidden) {
        if (searchable.find(token) != std::string::npos) {
            std::cerr << "Renderer link boundary found forbidden host token '"
                      << token << "' in " << path << ".\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Renderer link boundary requires at least one binary path.\n";
        return 1;
    }
    for (int index = 1; index < argc; ++index) {
        if (!verify_binary(argv[index])) {
            return 1;
        }
    }
    std::cout << "Renderer CPU link boundary passed for " << (argc - 1)
              << " binaries.\n";
    return 0;
}
