/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 09:05:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ShaderParser.hpp"

#include <Caramel/Core/Logger.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>

namespace fs = std::filesystem;

namespace {

std::optional<std::string> ReadFileText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

fs::path Canonicalize(const fs::path& path)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

// Recursively inlines `#include "..."` lines starting from `path`, writing the fully-inlined text
// into `outText`. Every absolute path read is appended (deduplicated) to `outDependencies`. A file
// already inlined earlier in this same parse pass is skipped (contributes nothing further) --
// harmless even without a guard since Content/Shaders/Common/AGFX.hlsli already has its own
// `#ifndef` guard, this just avoids needless symbol/text bloat.
bool InlineIncludesRecursive(const fs::path& path, TArray<String>& outDependencies, TArray<fs::path>& visited, String& outText)
{
    fs::path canonical = Canonicalize(path);

    for (auto& v : visited) {
        if (v == canonical) {
            outText.Clear();
            return true;
        }
    }
    visited.PushBack(canonical);

    auto contents = ReadFileText(canonical);
    if (!contents) {
        CARAMEL_ERROR("ShaderParser: failed to open '{}'", canonical.string());
        return false;
    }

    String canonicalStr = canonical.string();
    bool alreadyTracked = false;
    for (auto& dep : outDependencies) {
        if (dep == canonicalStr) { alreadyTracked = true; break; }
    }
    if (!alreadyTracked)
        outDependencies.PushBack(canonicalStr);

    static const std::regex includeRegex(R"re(^\s*#include\s*"([^"]+)"\s*$)re");

    std::istringstream stream(*contents);
    std::ostringstream result;
    std::string stdLine;
    while (std::getline(stream, stdLine)) {
        std::smatch match;
        if (std::regex_match(stdLine, match, includeRegex)) {
            fs::path includePath = canonical.parent_path() / match[1].str();
            String inlined;
            if (!InlineIncludesRecursive(includePath, outDependencies, visited, inlined))
                return false;
            result << inlined.CStr() << "\n";
        } else {
            result << stdLine << "\n";
        }
    }

    outText = result.str();
    return true;
}

std::optional<EShaderStage> StageFromPragmaName(const std::string& name)
{
    if (name == "vertex")  return EShaderStage::Vertex;
    if (name == "pixel")   return EShaderStage::Fragment;
    if (name == "compute") return EShaderStage::Compute;
    if (name == "task")    return EShaderStage::Task;
    if (name == "mesh")    return EShaderStage::Mesh;
    return std::nullopt;
}

} // namespace

std::optional<ParsedShaderSource> ParseShaderFile(const String& shaderPath)
{
    fs::path rootPath(shaderPath.CStr());
    fs::path canonicalRoot = Canonicalize(rootPath);

    ParsedShaderSource parsed;
    parsed.ShaderPath = canonicalRoot.string();

    TArray<fs::path> visited;
    String inlined;
    if (!InlineIncludesRecursive(rootPath, parsed.DependencyFiles, visited, inlined))
        return std::nullopt;

    // Scan the fully-inlined text for pragmas, blanking matched lines (kept as an empty line, not
    // removed, so DXC's error line numbers still map back to the original source).
    static const std::regex stagePragma(R"(^\s*#pragma\s+(vertex|pixel|compute|task|mesh)\s+(\w+)\s*$)");
    static const std::regex variantPragma(R"(^\s*#pragma\s+variant\s+(\w+)\s*$)");

    std::istringstream stream(inlined.CStr());
    std::ostringstream result;
    std::string stdLine;
    while (std::getline(stream, stdLine)) {
        std::smatch match;
        if (std::regex_match(stdLine, match, stagePragma)) {
            if (auto stage = StageFromPragmaName(match[1].str())) {
                if (!parsed.StageEntryPoints.Contains(*stage))
                    parsed.StageEntryPoints.Insert(*stage, String(match[2].str()));
            }
            result << "\n";
        } else if (std::regex_match(stdLine, match, variantPragma)) {
            String name(match[1].str());
            bool exists = false;
            for (auto& v : parsed.Variants) {
                if (v == name) { exists = true; break; }
            }
            if (!exists) {
                if (parsed.Variants.Size() >= kMaxShaderVariants) {
                    CARAMEL_ERROR("ShaderParser: '{}' declares more than {} variants, ignoring '{}'", shaderPath.CStr(), kMaxShaderVariants, name.CStr());
                } else {
                    parsed.Variants.PushBack(name);
                }
            }
            result << "\n";
        } else {
            result << stdLine << "\n";
        }
    }

    parsed.InlinedSource = result.str();
    return parsed;
}
