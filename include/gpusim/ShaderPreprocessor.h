#pragma once
#include <string>
#include <filesystem>
#include <vector>

namespace GPUSim {

    struct PreprocessResult {
        bool        success;
        std::string source;
        std::string error;
    };

    PreprocessResult PreprocessShader(
        const std::filesystem::path& filePath,
        const std::vector<std::string>& defines = {}
    );

} // namespace GPUSim