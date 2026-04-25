#pragma once

#include "ini.hpp"
#include "types.hpp"
#include "utility.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace le::cli {

inline bool prompt(const std::wstring& text, std::wstring* out_line) {
    std::wcout << text;
    std::wcout.flush();
    return static_cast<bool>(std::getline(std::wcin, *out_line));
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

    std::wcout << L"请选择 Profile（输入数字，0 取消）:\n";
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        std::wcout << (i + 1) << L". " << profiles[i].name << L" [" << profiles[i].guid << L"]\n";
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

        std::wcout << L"输入无效，请重新输入。\n";
    }
}

inline bool run_config_wizard(
    const std::filesystem::path& output_ini,
    const RuntimeConfig& defaults,
    std::wstring* error = nullptr) {
    std::wcout << L"== LocaleEmulator 配置向导 ==\n";
    std::wcout << L"直接回车可使用默认值。\n";

    const std::wstring default_install = defaults.install_path.has_value()
        ? defaults.install_path->wstring()
        : L"";
    const std::wstring default_mode = mode_to_wstring(defaults.mode);
    const std::wstring default_profile = defaults.profile_guid.value_or(L"");

    std::wstring install_path;
    std::wstring mode_text;
    std::wstring profile_guid;

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

    if (!prompt_with_default(L"ProfileGuid", default_profile, &profile_guid)) {
        if (error != nullptr) {
            *error = L"Input cancelled while reading ProfileGuid.";
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

    std::wstring read_error;
    ini::IniMap map = ini::read_ini(output_ini, &read_error);
    if (!read_error.empty()) {
        std::wcout << L"读取现有配置失败，将覆盖写入: " << read_error << L"\n";
    }

    if (!install_path.empty()) {
        ini::set_value(map, L"InstallPath", install_path);
    } else {
        ini::remove_key(map, L"InstallPath");
    }

    ini::set_value(map, L"Mode", mode_to_wstring(*parsed_mode));

    if (!profile_guid.empty()) {
        ini::set_value(map, L"ProfileGuid", profile_guid);
    } else {
        ini::remove_key(map, L"ProfileGuid");
    }

    if (!ini::write_ini(output_ini, map, error)) {
        return false;
    }

    std::wcout << L"配置已写入: " << output_ini.wstring() << L"\n";
    return true;
}

} // namespace le::cli
