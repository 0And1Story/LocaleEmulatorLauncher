#pragma once

#include "types.hpp"
#include "utility.hpp"

#include <filesystem>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace le::profile {

inline std::filesystem::path profile_xml_path_from_leproc(const std::filesystem::path& leproc_exe) {
    return leproc_exe.parent_path() / L"LEConfig.xml";
}

inline std::vector<ProfileEntry> load_profiles(const std::filesystem::path& xml_path, std::wstring* error = nullptr) {
    std::vector<ProfileEntry> profiles;
    if (!std::filesystem::exists(xml_path)) {
        if (error != nullptr) {
            *error = L"Profile config not found: " + xml_path.wstring();
        }
        return profiles;
    }

    std::wstring read_error;
    const std::wstring xml = util::read_file_text(xml_path, &read_error);
    if (!read_error.empty()) {
        if (error != nullptr) {
            *error = std::move(read_error);
        }
        return profiles;
    }

    static const std::wregex tag_regex(LR"(<[^>]+>)");
    static const std::wregex guid_regex(LR"(([Gg][Uu][Ii][Dd])\s*=\s*(['"])(.*?)\2)");
    static const std::wregex name_regex(LR"(([Nn][Aa][Mm][Ee])\s*=\s*(['"])(.*?)\2)");

    std::set<std::wstring> seen_guids;

    auto begin = std::wsregex_iterator(xml.begin(), xml.end(), tag_regex);
    auto end = std::wsregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::wstring tag = it->str();
        std::wsmatch guid_match;
        std::wsmatch name_match;
        if (!std::regex_search(tag, guid_match, guid_regex) ||
            !std::regex_search(tag, name_match, name_regex)) {
            continue;
        }

        std::wstring guid = util::trim(guid_match[3].str());
        std::wstring name = util::trim(name_match[3].str());
        guid = util::unescape_xml(guid);
        name = util::unescape_xml(name);
        if (guid.empty()) {
            continue;
        }

        const std::wstring normalized = util::to_lower_ascii(guid);
        if (!seen_guids.insert(normalized).second) {
            continue;
        }

        if (name.empty()) {
            name = L"(unnamed)";
        }
        profiles.push_back(ProfileEntry{
            .name = std::move(name),
            .guid = std::move(guid),
        });
    }

    if (profiles.empty() && error != nullptr) {
        *error = L"No profile entries found in " + xml_path.wstring();
    }
    return profiles;
}

} // namespace le::profile
