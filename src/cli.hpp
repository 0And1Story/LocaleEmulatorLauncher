#pragma once

#include "config.hpp"
#include "ini.hpp"
#include "le_profile.hpp"
#include "types.hpp"
#include "utility.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace le::cli {

inline bool prompt(const std::wstring& text, std::wstring* out_line) {
    if (!util::write_stdout(text)) {
        return false;
    }
    return util::read_console_line(out_line);
}

inline bool prompt_with_default(
    const std::wstring& label,
    const std::wstring& default_value,
    std::wstring* out_value) {
    std::wstring line;
    if (!prompt(label + L" [" + default_value + L"]: ", &line)) {
        return false;
    }
    line = util::trim(line);
    if (line.empty()) {
        *out_value = default_value;
    } else {
        *out_value = line;
    }
    return true;
}

inline std::optional<ProfileEntry> choose_profile(const std::vector<ProfileEntry>& profiles) {
    if (profiles.empty()) {
        return std::nullopt;
    }

    util::write_stdout(L"请选择 Profile（输入数字，0 取消）:\n");
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        util::write_stdout(
            std::to_wstring(i + 1) + L". " + profiles[i].name + L" [" + profiles[i].guid + L"]\n");
    }

    while (true) {
        std::wstring line;
        if (!prompt(L"选择: ", &line)) {
            return std::nullopt;
        }
        line = util::trim(line);
        if (line.empty()) {
            continue;
        }

        try {
            const unsigned long value = std::stoul(line);
            if (value == 0) {
                return std::nullopt;
            }
            if (value >= 1 && value <= profiles.size()) {
                return profiles[static_cast<std::size_t>(value - 1)];
            }
        } catch (...) {
        }

        util::write_stdout(L"输入无效，请重新输入。\n");
    }
}

inline std::optional<std::wstring> choose_profile_guid_with_default(
    const std::vector<ProfileEntry>& profiles,
    const std::optional<std::wstring>& default_guid) {
    if (profiles.empty()) {
        return std::nullopt;
    }

    util::write_stdout(L"请选择 Profile（输入数字，0 取消）:\n");
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        std::wstring marker;
        if (default_guid.has_value() &&
            util::iequals_ascii(util::trim(*default_guid), util::trim(profiles[i].guid))) {
            marker = L"  <- 当前默认";
        }
        util::write_stdout(
            std::to_wstring(i + 1) + L". " + profiles[i].name + L" [" + profiles[i].guid + L"]" + marker + L"\n");
    }

    if (default_guid.has_value() && !util::trim(*default_guid).empty()) {
        util::write_stdout(L"直接回车保留当前默认 Profile。\n");
    }

    while (true) {
        std::wstring line;
        if (!prompt(L"选择: ", &line)) {
            return std::nullopt;
        }

        line = util::trim(line);
        if (line.empty()) {
            if (default_guid.has_value()) {
                const std::wstring trimmed = util::trim(*default_guid);
                if (!trimmed.empty()) {
                    return trimmed;
                }
            }
            util::write_stdout(L"请输入序号，或输入 0 取消。\n");
            continue;
        }

        try {
            const unsigned long value = std::stoul(line);
            if (value == 0) {
                return std::nullopt;
            }
            if (value >= 1 && value <= profiles.size()) {
                return profiles[static_cast<std::size_t>(value - 1)].guid;
            }
        } catch (...) {
        }

        util::write_stdout(L"输入无效，请重新输入。\n");
    }
}

inline bool run_config_wizard(
    const std::filesystem::path& output_ini,
    const RuntimeConfig& defaults,
    std::wstring* error = nullptr) {
    util::write_stdout(L"== LocaleEmulator 配置向导 ==\n");
    util::write_stdout(L"直接回车可使用默认值。\n");

    const std::wstring default_install = defaults.install_path.has_value()
        ? defaults.install_path->wstring()
        : L"";
    const std::wstring default_mode = mode_to_wstring(defaults.mode);
    const std::wstring initial_default_profile = defaults.profile_guid.value_or(L"");

    std::wstring install_path;
    std::wstring mode_text;
    std::wstring profile_guid = initial_default_profile;

    if (!prompt_with_default(L"InstallPath", default_install, &install_path)) {
        if (error != nullptr) {
            *error = L"Input cancelled while reading InstallPath.";
        }
        return false;
    }

    if (!prompt_with_default(L"Mode(path/run/runas/manage/global)", default_mode, &mode_text)) {
        if (error != nullptr) {
            *error = L"Input cancelled while reading Mode.";
        }
        return false;
    }

    const std::optional<Mode> parsed_mode = parse_mode(mode_text);
    if (!parsed_mode.has_value()) {
        if (error != nullptr) {
            *error = L"Invalid Mode: " + mode_text;
        }
        return false;
    }

    const std::optional<std::wstring> default_profile =
        util::trim(initial_default_profile).empty()
        ? std::nullopt
        : std::optional<std::wstring>(util::trim(initial_default_profile));

    if (mode_requires_guid(*parsed_mode)) {
        bool selected_from_list = false;

        if (!install_path.empty()) {
            RuntimeConfig probe;
            probe.install_path = std::filesystem::path(install_path);
            probe.mode = *parsed_mode;
            probe.profile_guid = default_profile;

            std::wstring ignored;
            const std::optional<std::filesystem::path> leproc = config::resolve_leproc_exe(probe, &ignored);
            if (leproc.has_value()) {
                std::wstring profile_error;
                const std::vector<ProfileEntry> profiles =
                    profile::load_profiles(profile::profile_xml_path_from_leproc(*leproc), &profile_error);
                if (!profiles.empty()) {
                    const std::optional<std::wstring> selected =
                        choose_profile_guid_with_default(profiles, default_profile);
                    if (!selected.has_value()) {
                        if (error != nullptr) {
                            *error = L"Profile selection cancelled.";
                        }
                        return false;
                    }
                    profile_guid = *selected;
                    selected_from_list = true;
                }
            }
        }

        if (!selected_from_list) {
            const std::wstring fallback_default = default_profile.value_or(L"");
            if (!prompt_with_default(L"ProfileGuid", fallback_default, &profile_guid)) {
                if (error != nullptr) {
                    *error = L"Input cancelled while reading ProfileGuid.";
                }
                return false;
            }
        }
    } else {
        const std::wstring fallback_default = default_profile.value_or(L"");
        if (!prompt_with_default(L"ProfileGuid", fallback_default, &profile_guid)) {
            if (error != nullptr) {
                *error = L"Input cancelled while reading ProfileGuid.";
            }
            return false;
        }
    }

    std::wstring read_error;
    ini::IniReadResult read_result = ini::read_ini(output_ini, ini::runtime_ini_registry(), &read_error);
    if (!read_error.empty()) {
        util::write_stdout(L"读取现有配置失败，将覆盖写入: " + read_error + L"\n");
    }
    for (const std::wstring& warning : read_result.warnings) {
        util::write_stdout(L"提示: " + warning + L"\n");
    }

    if (!install_path.empty()) {
        ini::set_value(read_result.values, L"InstallPath", install_path);
    } else {
        ini::remove_key(read_result.values, L"InstallPath");
    }

    ini::set_value(read_result.values, L"Mode", mode_to_wstring(*parsed_mode));

    if (!profile_guid.empty()) {
        ini::set_value(read_result.values, L"ProfileGuid", profile_guid);
    } else {
        ini::remove_key(read_result.values, L"ProfileGuid");
    }

    if (!ini::write_ini(output_ini, read_result.values, ini::runtime_ini_registry(), error)) {
        return false;
    }

    util::write_stdout(L"配置已写入: " + output_ini.wstring() + L"\n");
    return true;
}

} // namespace le::cli
