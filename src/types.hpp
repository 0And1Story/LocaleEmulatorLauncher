#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace le {

enum class Mode {
    Path,
    Run,
    RunAs,
    Manage,
    Global,
};

inline std::wstring to_lower_ascii(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            out.push_back(static_cast<wchar_t>(ch - L'A' + L'a'));
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

inline std::optional<Mode> parse_mode(std::wstring_view value) {
    const std::wstring lower = to_lower_ascii(value);
    if (lower == L"path") {
        return Mode::Path;
    }
    if (lower == L"run") {
        return Mode::Run;
    }
    if (lower == L"runas") {
        return Mode::RunAs;
    }
    if (lower == L"manage") {
        return Mode::Manage;
    }
    if (lower == L"global") {
        return Mode::Global;
    }
    return std::nullopt;
}

inline std::wstring mode_to_wstring(Mode mode) {
    switch (mode) {
    case Mode::Path:
        return L"path";
    case Mode::Run:
        return L"run";
    case Mode::RunAs:
        return L"runas";
    case Mode::Manage:
        return L"manage";
    case Mode::Global:
        return L"global";
    default:
        return L"runas";
    }
}

inline bool mode_requires_guid(Mode mode) {
    return mode == Mode::RunAs;
}

struct CliOptions {
    bool show_help = false;
    bool run_config_wizard = false;
    std::optional<std::filesystem::path> target_input;
    std::vector<std::wstring> target_passthrough_args;
    std::optional<std::filesystem::path> install_path;
    std::optional<std::wstring> profile_guid;
    std::optional<Mode> mode;
};

struct ParseResult {
    bool ok = false;
    CliOptions options;
    std::wstring error;
};

struct RuntimeConfig {
    std::optional<std::filesystem::path> install_path;
    std::optional<std::wstring> profile_guid;
    Mode mode = Mode::RunAs;
};

struct TargetSpec {
    std::filesystem::path original_input;
    std::filesystem::path actual_path;
    std::filesystem::path working_dir;
    std::vector<std::wstring> launch_args;
};

struct ProfileEntry {
    std::wstring name;
    std::wstring guid;
};

struct LaunchPlan {
    std::filesystem::path leproc_exe;
    std::vector<std::wstring> arguments;
    std::filesystem::path working_dir;
};

} // namespace le
