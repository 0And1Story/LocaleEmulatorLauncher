#pragma once

#include "ini.hpp"
#include "types.hpp"
#include "utility.hpp"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cwchar>

namespace le::config {

inline void append_warning(std::wstring* warnings, const std::wstring& line) {
    if (warnings == nullptr) {
        return;
    }
    if (!warnings->empty()) {
        *warnings += L"\n";
    }
    *warnings += line;
}

inline void apply_ini_overrides(const ini::IniMap& map, RuntimeConfig& config, std::wstring* warnings = nullptr) {
    if (const auto value = ini::get_value(map, L"InstallPath"); value.has_value()) {
        const std::wstring trimmed = util::trim(*value);
        if (!trimmed.empty()) {
            config.install_path = std::filesystem::path(trimmed);
        }
    }

    if (const auto value = ini::get_value(map, L"ProfileGuid"); value.has_value()) {
        const std::wstring trimmed = util::trim(*value);
        if (!trimmed.empty()) {
            config.profile_guid = trimmed;
        }
    }

    if (const auto value = ini::get_value(map, L"Mode"); value.has_value()) {
        const std::wstring trimmed = util::trim(*value);
        if (!trimmed.empty()) {
            const std::optional<Mode> mode = parse_mode(trimmed);
            if (mode.has_value()) {
                config.mode = *mode;
            } else {
                append_warning(warnings, L"Ignored invalid Mode in ini: " + trimmed);
            }
        }
    }
}

inline RuntimeConfig load_runtime_from_ini_file(
    const std::filesystem::path& ini_path,
    std::wstring* warnings = nullptr) {
    RuntimeConfig config;
    config.mode = Mode::RunAs;

    std::wstring read_error;
    const ini::IniReadResult read_result = ini::read_ini(ini_path, ini::runtime_ini_registry(), &read_error);
    if (!read_error.empty()) {
        append_warning(warnings, read_error);
    }
    for (const std::wstring& warning : read_result.warnings) {
        append_warning(warnings, warning);
    }
    apply_ini_overrides(read_result.values, config, warnings);
    return config;
}

inline RuntimeConfig load_runtime_config(
    const CliOptions& cli,
    const std::filesystem::path& launcher_dir,
    const std::optional<std::filesystem::path>& target_dir,
    std::optional<std::filesystem::path>* target_ini_used = nullptr,
    std::wstring* warnings = nullptr) {
    RuntimeConfig config;
    config.mode = Mode::RunAs;

    const std::filesystem::path launcher_ini = launcher_dir / L"config.ini";
    {
        std::wstring read_error;
        const ini::IniReadResult read_result = ini::read_ini(launcher_ini, ini::runtime_ini_registry(), &read_error);
        if (!read_error.empty()) {
            append_warning(warnings, read_error);
        }
        for (const std::wstring& warning : read_result.warnings) {
            append_warning(warnings, warning);
        }
        apply_ini_overrides(read_result.values, config, warnings);
    }

    if (target_dir.has_value()) {
        const std::filesystem::path local_ini = *target_dir / L"leconfig.ini";
        if (std::filesystem::exists(local_ini)) {
            std::wstring read_error;
            const ini::IniReadResult read_result = ini::read_ini(local_ini, ini::runtime_ini_registry(), &read_error);
            if (!read_error.empty()) {
                append_warning(warnings, read_error);
            }
            for (const std::wstring& warning : read_result.warnings) {
                append_warning(warnings, warning);
            }
            apply_ini_overrides(read_result.values, config, warnings);
            if (target_ini_used != nullptr) {
                *target_ini_used = local_ini;
            }
        }
    }

    if (cli.install_path.has_value()) {
        config.install_path = cli.install_path;
    }
    if (cli.profile_guid.has_value()) {
        const std::wstring trimmed = util::trim(*cli.profile_guid);
        if (!trimmed.empty()) {
            config.profile_guid = trimmed;
        }
    }
    if (cli.mode.has_value()) {
        config.mode = *cli.mode;
    }

    return config;
}

inline std::optional<std::filesystem::path> validate_install_dir(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::filesystem::path leproc = dir / L"LEProc.exe";
    const std::filesystem::path leconfig = dir / L"LEConfig.xml";
    if (!std::filesystem::is_regular_file(leproc, ec) || !std::filesystem::is_regular_file(leconfig, ec)) {
        return std::nullopt;
    }

    const std::filesystem::path absolute = std::filesystem::absolute(leproc, ec);
    if (!ec) {
        return absolute;
    }
    return leproc;
}

inline std::optional<std::filesystem::path> resolve_leproc_from_install_path(const std::filesystem::path& install_path) {
    if (install_path.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    if (std::filesystem::is_regular_file(install_path, ec)) {
        if (!util::iequals_ascii(install_path.filename().wstring(), L"LEProc.exe")) {
            return std::nullopt;
        }
        return validate_install_dir(install_path.parent_path());
    }
    return validate_install_dir(install_path);
}

inline std::optional<std::filesystem::path> find_leproc_in_launcher_dir() {
    return validate_install_dir(util::executable_dir());
}

inline std::optional<std::filesystem::path> find_leproc_in_path() {
    const auto path_env = util::getenv_w(L"PATH");
    if (!path_env.has_value()) {
        return std::nullopt;
    }

    std::size_t start = 0;
    while (start <= path_env->size()) {
        std::size_t end = path_env->find(L';', start);
        if (end == std::wstring::npos) {
            end = path_env->size();
        }

        std::wstring segment = util::trim(path_env->substr(start, end - start));
        if (!segment.empty() && segment.front() == L'"' && segment.back() == L'"' && segment.size() >= 2) {
            segment = segment.substr(1, segment.size() - 2);
        }

        if (!segment.empty()) {
            if (const std::optional<std::filesystem::path> found = validate_install_dir(std::filesystem::path(segment));
                found.has_value()) {
                return found;
            }
        }

        if (end == path_env->size()) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

inline std::optional<std::filesystem::path> find_leproc_in_common_dirs() {

    std::vector<std::filesystem::path> roots;
    for (const wchar_t* env_name : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"}) {
        if (const auto value = util::getenv_w(env_name); value.has_value()) {
            const std::filesystem::path root(*value);
            if (!root.empty()) {
                roots.push_back(root);
            }
        }
    }

    wchar_t drives[512] = {};
    const DWORD drive_len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    if (drive_len > 0 && drive_len < std::size(drives)) {
        const wchar_t* ptr = drives;
        while (*ptr != L'\0') {
            std::filesystem::path drive_root(ptr);
            roots.push_back(drive_root / L"Program Files");
            roots.push_back(drive_root / L"Program Files (x86)");
            ptr += std::wcslen(ptr) + 1;
        }
    }

    for (const auto& root : roots) {
        for (const wchar_t* folder : {L"LocaleEmulator", L"Locale Emulator"}) {
            if (const std::optional<std::filesystem::path> found = validate_install_dir(root / folder);
                found.has_value()) {
                return found;
            }
        }
    }
    return std::nullopt;
}

inline std::optional<std::filesystem::path> resolve_leproc_exe(RuntimeConfig& config, std::wstring* diagnostic = nullptr) {
    if (config.install_path.has_value()) {
        const std::optional<std::filesystem::path> candidate = resolve_leproc_from_install_path(*config.install_path);
        if (candidate.has_value()) {
            config.install_path = candidate->parent_path();
            return candidate;
        }
        append_warning(diagnostic, L"Configured InstallPath is missing LEProc.exe or LEConfig.xml, fallback to auto-discovery.");
    }

    if (const auto by_launcher_dir = find_leproc_in_launcher_dir(); by_launcher_dir.has_value()) {
        config.install_path = by_launcher_dir->parent_path();
        return by_launcher_dir;
    }

    if (const auto by_path = find_leproc_in_path(); by_path.has_value()) {
        config.install_path = by_path->parent_path();
        return by_path;
    }
    if (const auto by_common = find_leproc_in_common_dirs(); by_common.has_value()) {
        config.install_path = by_common->parent_path();
        return by_common;
    }

    append_warning(diagnostic, L"Unable to discover LEProc.exe + LEConfig.xml from launcher dir, PATH, or common install directories.");
    return std::nullopt;
}

inline std::filesystem::path choose_profile_persist_ini(
    const std::filesystem::path& launcher_dir,
    const std::optional<std::filesystem::path>& target_dir) {
    if (target_dir.has_value()) {
        return *target_dir / L"leconfig.ini";
    }
    return launcher_dir / L"config.ini";
}

inline bool persist_profile_guid(const std::filesystem::path& ini_path, const std::wstring& guid, std::wstring* error = nullptr) {
    std::wstring read_error;
    ini::IniReadResult read_result = ini::read_ini(ini_path, ini::runtime_ini_registry(), &read_error);
    if (!read_error.empty()) {
        append_warning(error, read_error);
    }
    for (const std::wstring& warning : read_result.warnings) {
        append_warning(error, warning);
    }

    ini::set_value(read_result.values, L"ProfileGuid", guid);
    return ini::write_ini(ini_path, read_result.values, ini::runtime_ini_registry(), error);
}

} // namespace le::config
