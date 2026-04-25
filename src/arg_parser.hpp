#pragma once

#include "types.hpp"
#include "utility.hpp"

#include <string>

namespace le {

inline std::wstring usage_text() {
    return LR"(LocaleEmulator launcher

Usage:
  LocaleEmulator.exe [options] <target.exe|target.lnk> [target args...]
  LocaleEmulator.exe [options] --mode global
  LocaleEmulator.exe --config

Options:
  --install-path <path>   LocaleEmulator install directory or LEProc.exe path
  --lepath <path>         Alias of --install-path
  --profile-guid <guid>   Profile GUID for runas mode
  --profile <guid>        Alias of --profile-guid
  --mode <mode>           path | run | runas | manage | global
  --config                Interactive wizard, write config.ini in current directory
  --help, -h, /?          Show this help
)";
}

inline bool option_match(const std::wstring& arg, std::wstring_view option_name) {
    if (arg == option_name) {
        return true;
    }
    if (arg.size() > option_name.size() + 1 &&
        arg.compare(0, option_name.size(), option_name) == 0 &&
        arg[option_name.size()] == L'=') {
        return true;
    }
    return false;
}

inline std::optional<std::wstring> extract_option_value(
    int& index,
    int argc,
    wchar_t* argv[],
    const std::wstring& current_arg,
    std::wstring_view option_name,
    std::wstring& error) {
    if (current_arg.size() > option_name.size() + 1 &&
        current_arg.compare(0, option_name.size(), option_name) == 0 &&
        current_arg[option_name.size()] == L'=') {
        return current_arg.substr(option_name.size() + 1);
    }

    if (index + 1 >= argc) {
        error = L"Option requires a value: " + std::wstring(option_name);
        return std::nullopt;
    }

    ++index;
    return std::wstring(argv[index]);
}

inline ParseResult parse_args(int argc, wchar_t* argv[]) {
    ParseResult result;
    result.ok = true;

    bool force_passthrough = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];

        if (force_passthrough) {
            if (!result.options.target_input.has_value()) {
                result.ok = false;
                result.error = L"`--` used before target path.";
                return result;
            }
            result.options.target_passthrough_args.push_back(arg);
            continue;
        }

        if (!result.options.target_input.has_value() && arg == L"--") {
            force_passthrough = true;
            continue;
        }

        if (!result.options.target_input.has_value() && (arg == L"--help" || arg == L"-h" || arg == L"/?")) {
            result.options.show_help = true;
            continue;
        }

        if (!result.options.target_input.has_value() && arg == L"--config") {
            result.options.run_config_wizard = true;
            continue;
        }

        if (!result.options.target_input.has_value() &&
            (option_match(arg, L"--install-path") || option_match(arg, L"--lepath"))) {
            std::wstring error;
            std::optional<std::wstring> value;
            if (option_match(arg, L"--install-path")) {
                value = extract_option_value(i, argc, argv, arg, L"--install-path", error);
            } else {
                value = extract_option_value(i, argc, argv, arg, L"--lepath", error);
            }
            if (!value.has_value()) {
                result.ok = false;
                result.error = error;
                return result;
            }
            result.options.install_path = std::filesystem::path(*value);
            continue;
        }

        if (!result.options.target_input.has_value() &&
            (option_match(arg, L"--profile-guid") || option_match(arg, L"--profile"))) {
            std::wstring error;
            std::optional<std::wstring> value;
            if (option_match(arg, L"--profile-guid")) {
                value = extract_option_value(i, argc, argv, arg, L"--profile-guid", error);
            } else {
                value = extract_option_value(i, argc, argv, arg, L"--profile", error);
            }
            if (!value.has_value()) {
                result.ok = false;
                result.error = error;
                return result;
            }
            result.options.profile_guid = util::trim(*value);
            continue;
        }

        if (!result.options.target_input.has_value() && option_match(arg, L"--mode")) {
            std::wstring error;
            const std::optional<std::wstring> value = extract_option_value(i, argc, argv, arg, L"--mode", error);
            if (!value.has_value()) {
                result.ok = false;
                result.error = error;
                return result;
            }

            const std::optional<Mode> mode = parse_mode(util::trim(*value));
            if (!mode.has_value()) {
                result.ok = false;
                result.error = L"Invalid mode: " + *value;
                return result;
            }
            result.options.mode = mode;
            continue;
        }

        if (!result.options.target_input.has_value() && !arg.empty() && arg.front() == L'-') {
            result.ok = false;
            result.error = L"Unknown option: " + arg;
            return result;
        }

        if (!result.options.target_input.has_value()) {
            result.options.target_input = std::filesystem::path(arg);
            continue;
        }

        result.options.target_passthrough_args.push_back(arg);
    }

    return result;
}

} // namespace le
