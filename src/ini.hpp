#pragma once

#include "utility.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace le::ini {

using IniMap = std::unordered_map<std::wstring, std::wstring>;

inline std::wstring normalize_key(std::wstring_view key) {
    return util::to_lower_ascii(util::trim(key));
}

inline std::wstring display_key(std::wstring_view normalized_key) {
    if (normalized_key == L"installpath") {
        return L"InstallPath";
    }
    if (normalized_key == L"profileguid") {
        return L"ProfileGuid";
    }
    if (normalized_key == L"mode") {
        return L"Mode";
    }
    return std::wstring(normalized_key);
}

inline std::optional<std::wstring> get_value(const IniMap& map, std::wstring_view key) {
    const auto it = map.find(normalize_key(key));
    if (it == map.end()) {
        return std::nullopt;
    }
    return it->second;
}

inline void set_value(IniMap& map, std::wstring_view key, std::wstring value) {
    map[normalize_key(key)] = std::move(value);
}

inline void remove_key(IniMap& map, std::wstring_view key) {
    map.erase(normalize_key(key));
}

inline IniMap read_ini(const std::filesystem::path& path, std::wstring* error = nullptr) {
    IniMap out;
    if (!std::filesystem::exists(path)) {
        return out;
    }

    std::wstring read_error;
    const std::wstring text = util::read_file_text(path, &read_error);
    if (!read_error.empty()) {
        if (error != nullptr) {
            *error = std::move(read_error);
        }
        return out;
    }

    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }

        const std::wstring trimmed = util::trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == L';' || trimmed.front() == L'#') {
            continue;
        }
        if (trimmed.front() == L'[') {
            continue;
        }

        const std::size_t eq = trimmed.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }

        const std::wstring key = normalize_key(trimmed.substr(0, eq));
        const std::wstring value = util::trim(trimmed.substr(eq + 1));
        if (!key.empty()) {
            out[key] = value;
        }
    }
    return out;
}

inline bool write_ini(const std::filesystem::path& path, const IniMap& data, std::wstring* error = nullptr) {
    std::wstring content;

    const std::array<std::wstring_view, 3> priority_keys = {
        L"installpath", L"profileguid", L"mode"
    };

    for (const std::wstring_view key : priority_keys) {
        const auto it = data.find(std::wstring(key));
        if (it != data.end()) {
            content += display_key(key);
            content += L"=";
            content += it->second;
            content += L"\n";
        }
    }

    std::map<std::wstring, std::wstring> extras;
    for (const auto& [key, value] : data) {
        if (key == L"installpath" || key == L"profileguid" || key == L"mode") {
            continue;
        }
        extras.emplace(key, value);
    }
    for (const auto& [key, value] : extras) {
        content += display_key(key);
        content += L"=";
        content += value;
        content += L"\n";
    }

    return util::write_file_utf8(path, content, error);
}

} // namespace le::ini
