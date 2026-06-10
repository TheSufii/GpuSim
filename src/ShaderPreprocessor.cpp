#include "ShaderPreprocessor.h"
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace GPUSim {

    struct PreprocessState {
        std::unordered_set<std::string>      guardSet;
        std::unordered_map<std::string, int> fileIndex;
    };

    static std::string ReadFile(const std::filesystem::path& path, std::string& err) {
        std::ifstream f(path);
        if (!f.is_open()) {
            err = "Could not open: " + path.string();
            return {};
        }
        return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
    }

    static std::string ProcessFile(
        const std::filesystem::path& filePath,
        PreprocessState& state,
        std::string& err)
    {
        std::string canonical = std::filesystem::weakly_canonical(filePath).string();

        if (state.guardSet.count(canonical)) return {};
        state.guardSet.insert(canonical);

        int thisIndex = static_cast<int>(state.fileIndex.size());
        state.fileIndex[canonical] = thisIndex;

        std::string raw = ReadFile(filePath, err);
        if (!err.empty()) return {};

        std::filesystem::path dir = filePath.parent_path();
        std::ostringstream    out;
        std::istringstream    in(raw);
        std::string           line;
        int                   lineNum = 1;

        out << "#line 1 " << thisIndex << "\n";

        while (std::getline(in, line)) {
            std::string_view trimmed(line);
            size_t first = trimmed.find_first_not_of(" \t");
            if (first != std::string_view::npos)
                trimmed.remove_prefix(first);

            if (trimmed.starts_with("#include")) {
                auto start = line.find_first_of("\"<");
                auto end = line.find_last_of("\">");

                if (start == std::string::npos || end == std::string::npos || end <= start) {
                    err = filePath.string() + ":" + std::to_string(lineNum)
                        + " - malformed #include";
                    return {};
                }

                std::string includeName = line.substr(start + 1, end - start - 1);
                std::filesystem::path includePath = dir / includeName;

                std::string included = ProcessFile(includePath, state, err);
                if (!err.empty()) return {};

                out << included;
                out << "#line " << (lineNum + 1) << " " << thisIndex << "\n";
            }
            else {
                out << line << "\n";
            }

            ++lineNum;
        }

        return out.str();
    }

    // returns true if `line`, ignoring leading whitespace, is a #version directive
    static bool IsVersionLine(std::string_view line) {
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos) return false;
        line.remove_prefix(first);
        return line.starts_with("#version");
    }

    // lifts the first #version directive out of `body` and returns it.
    // all #version lines are removed from `body`; the first one is returned.
    // if none is found, returns a sensible default. duplicates are dropped
    // (multiple #version directives are illegal in GLSL).
    static std::string HoistVersion(std::string& body) {
        std::istringstream in(body);
        std::ostringstream out;
        std::string        line;
        std::string        version;

        while (std::getline(in, line)) {
            if (IsVersionLine(line)) {
                if (version.empty())
                    version = line;     // keep the first, drop the rest
                continue;               // strip it from the body either way
            }
            out << line << "\n";
        }

        body = out.str();
        return version.empty() ? "#version 450 core" : version;
    }

    PreprocessResult PreprocessShader(
        const std::filesystem::path& filePath,
        const std::vector<std::string>& defines)
    {
        if (!std::filesystem::exists(filePath))
            return { false, {}, "File not found: " + filePath.string() };

        PreprocessState state;
        std::string     err;

        std::string body = ProcessFile(filePath, state, err);
        if (!err.empty()) return { false, {}, err };

        // GLSL requires #version to be the first token. lift it out of the
        // assembled body (wherever it appeared) and emit it first, ahead of
        // injected defines and #line directives.
        std::string version = HoistVersion(body);

        std::ostringstream out;
        out << version << "\n";
        for (auto& def : defines)
            out << "#define " << def << "\n";
        out << body;

        return { true, out.str(), {} };
    }

} 