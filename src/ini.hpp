#pragma once

#include "utility.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace le::ini {

using IniMap = std::unordered_map<std::wstring, std::wstring>;

struct IniKeySpec {
    std::wstring canonical_key;
    std::vector<std::wstring> aliases;
};

class IniRegistry {
public:
    bool add(std::wstring canonical_key, std::vector<std::wstring> aliases = {}) {
        canonical_key = util::trim(canonical_key);
        if (canonical_key.empty()) {
            return false;
        }

        const std::wstring normalized_canonical = normalize_key(canonical_key);
        if (normalized_canonical.empty() || key_index_.contains(normalized_canonical)) {
            return false;
        }

        for (std::wstring& alias : aliases) {
            alias = util::trim(alias);
            if (alias.empty()) {
                return false;
            }
            const std::wstring normalized_alias = normalize_key(alias);
            if (key_index_.contains(normalized_alias)) {
                return false;
            }
        }

        const std::size_t index = specs_.size();
        specs_.push_back(IniKeySpec{
            .canonical_key = canonical_key,
            .aliases = std::move(aliases),
        });
        key_index_[normalized_canonical] = index;
        for (const std::wstring& alias : specs_[index].aliases) {
            key_index_[normalize_key(alias)] = index;
        }
        return true;
    }

    std::optional<std::wstring> resolve(std::wstring_view key) const {
        const auto it = key_index_.find(normalize_key(key));
        if (it == key_index_.end()) {
            return std::nullopt;
        }
        return specs_[it->second].canonical_key;
    }

    const std::vector<IniKeySpec>& specs() const {
        return specs_;
    }

private:
    std::vector<IniKeySpec> specs_;
    std::unordered_map<std::wstring, std::size_t> key_index_;

    static std::wstring normalize_key(std::wstring_view key) {
        return util::to_lower_ascii(util::trim(key));
    }
};

inline IniRegistry make_runtime_ini_registry() {
    IniRegistry registry;
    registry.add(L"InstallPath");
    registry.add(L"ProfileGuid");
    registry.add(L"Mode");
    return registry;
}

inline const IniRegistry& runtime_ini_registry() {
    static const IniRegistry registry = make_runtime_ini_registry();
    return registry;
}

struct IniReadResult {
    IniMap values;
    std::vector<std::wstring> warnings;
};

inline std::optional<std::wstring> get_value(const IniMap& map, std::wstring_view canonical_key) {
    const auto it = map.find(std::wstring(canonical_key));
    if (it == map.end()) {
        return std::nullopt;
    }
    return it->second;
}

inline void set_value(IniMap& map, std::wstring_view canonical_key, std::wstring value) {
    map[std::wstring(canonical_key)] = std::move(value);
}

inline void remove_key(IniMap& map, std::wstring_view canonical_key) {
    map.erase(std::wstring(canonical_key));
}

inline IniReadResult read_ini(
    const std::filesystem::path& path,
    const IniRegistry& registry,
    std::wstring* error = nullptr) {
    IniReadResult out;
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
    std::size_t line_no = 0;

    while (std::getline(stream, line)) {
        ++line_no;
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
            out.warnings.push_back(L"Ignored malformed line " + std::to_wstring(line_no) + L" in " + path.wstring());
            continue;
        }

        const std::wstring raw_key = util::trim(trimmed.substr(0, eq));
        const std::wstring value = util::trim(trimmed.substr(eq + 1));
        if (raw_key.empty()) {
            continue;
        }

        const std::optional<std::wstring> canonical = registry.resolve(raw_key);
        if (!canonical.has_value()) {
            out.warnings.push_back(L"Ignored unsupported key `" + raw_key + L"` in " + path.wstring());
            continue;
        }
        out.values[*canonical] = value;
    }

    return out;
}

inline bool write_ini(
    const std::filesystem::path& path,
    const IniMap& data,
    const IniRegistry& registry,
    std::wstring* error = nullptr) {
    std::wstring content;

    for (const IniKeySpec& spec : registry.specs()) {
        const auto it = data.find(spec.canonical_key);
        if (it == data.end()) {
            continue;
        }
        content += spec.canonical_key;
        content += L"=";
        content += it->second;
        content += L"\n";
    }

    std::map<std::wstring, std::wstring> extras;
    for (const auto& [key, value] : data) {
        if (registry.resolve(key).has_value()) {
            continue;
        }
        extras.emplace(key, value);
    }
    for (const auto& [key, value] : extras) {
        content += key;
        content += L"=";
        content += value;
        content += L"\n";
    }

    return util::write_file_utf8(path, content, error);
}

} // namespace le::ini
