#pragma once

#include "types.hpp"
#include "utility.hpp"

#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace le {

struct OptionSpec {
    std::wstring name;
    std::vector<std::wstring> aliases;
    bool takes_value = false;
    std::wstring value_hint;
    std::wstring description;
    std::wstring default_text;
    std::function<bool(CliOptions&, const std::optional<std::wstring>&, std::wstring&)> handler;
};

class OptionRegistry {
public:
    bool add(OptionSpec spec) {
        if (spec.name.empty() || (spec.name.front() != L'-' && spec.name.front() != L'/')) {
            return false;
        }
        const std::wstring primary = util::to_lower_ascii(spec.name);
        if (index_.contains(primary)) {
            return false;
        }

        for (std::wstring& alias : spec.aliases) {
            alias = util::trim(alias);
            if (alias.empty()) {
                return false;
            }
            if (alias.front() != L'-' && alias.front() != L'/') {
                return false;
            }
            const std::wstring key = util::to_lower_ascii(alias);
            if (index_.contains(key)) {
                return false;
            }
        }

        const std::size_t idx = specs_.size();
        specs_.push_back(std::move(spec));
        index_[primary] = idx;
        for (const std::wstring& alias : specs_[idx].aliases) {
            index_[util::to_lower_ascii(alias)] = idx;
        }
        return true;
    }

    const OptionSpec* find(std::wstring_view name) const {
        const std::wstring key = util::to_lower_ascii(name);
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        return &specs_[it->second];
    }

    const std::vector<OptionSpec>& specs() const {
        return specs_;
    }

private:
    std::vector<OptionSpec> specs_;
    std::unordered_map<std::wstring, std::size_t> index_;
};

inline OptionRegistry make_default_option_registry() {
    OptionRegistry registry;

    registry.add(OptionSpec {
        .name = L"--help",
        .aliases = {L"-h", L"/?"},
        .takes_value = false,
        .description = L"Show this help",
        .handler = [](CliOptions& options, const std::optional<std::wstring>&, std::wstring&) {
            options.show_help = true;
            return true;
        },
    });

    registry.add(OptionSpec {
        .name = L"--config",
        .takes_value = false,
        .description = L"Interactive wizard, write config.ini in launcher dir or leconfig.ini in other dirs",
        .handler = [](CliOptions& options, const std::optional<std::wstring>&, std::wstring&) {
            options.run_config_wizard = true;
            return true;
        },
    });

    registry.add(OptionSpec {
        .name = L"--lepath",
        .takes_value = true,
        .value_hint = L"<path>",
        .description = L"LocaleEmulator install directory or LEProc.exe path",
        .handler = [](CliOptions& options, const std::optional<std::wstring>& value, std::wstring& error) {
            if (!value.has_value()) {
                error = L"--lepath requires a value.";
                return false;
            }
            const std::wstring trimmed = util::trim(*value);
            if (trimmed.empty()) {
                error = L"--lepath cannot be empty.";
                return false;
            }
            options.install_path = std::filesystem::path(trimmed);
            return true;
        },
    });

    registry.add(OptionSpec {
        .name = L"--profile",
        .takes_value = true,
        .value_hint = L"<guid>",
        .description = L"Profile GUID for runas mode",
        .handler = [](CliOptions& options, const std::optional<std::wstring>& value, std::wstring& error) {
            if (!value.has_value()) {
                error = L"--profile requires a value.";
                return false;
            }
            const std::wstring trimmed = util::trim(*value);
            if (trimmed.empty()) {
                error = L"--profile cannot be empty.";
                return false;
            }
            options.profile_guid = trimmed;
            return true;
        },
    });

    registry.add(OptionSpec {
        .name = L"--mode",
        .takes_value = true,
        .value_hint = L"<mode>",
        .description = L"path | run | runas | manage | global",
        .default_text = L"runas",
        .handler = [](CliOptions& options, const std::optional<std::wstring>& value, std::wstring& error) {
            if (!value.has_value()) {
                error = L"--mode requires a value.";
                return false;
            }
            const std::optional<Mode> mode = parse_mode(util::trim(*value));
            if (!mode.has_value()) {
                error = L"Invalid mode: " + *value;
                return false;
            }
            options.mode = *mode;
            return true;
        },
    });

    return registry;
}

inline const OptionRegistry& default_option_registry() {
    static const OptionRegistry registry = make_default_option_registry();
    return registry;
}

inline std::wstring usage_text(const OptionRegistry& registry = default_option_registry()) {
    std::wostringstream out;
    out << LR"(Locale Emulator Launcher

Usage:
  LocaleEmulator.exe [options] <target.exe|target.lnk> [target args...]
  LocaleEmulator.exe [options] --mode global
  LocaleEmulator.exe --config

Options:
)";

    for (const OptionSpec& spec : registry.specs()) {
        out << L"  " << spec.name;
        for (const std::wstring& alias : spec.aliases) {
            out << L", " << alias;
        }
        if (spec.takes_value) {
            out << L" " << (spec.value_hint.empty() ? L"<value>" : spec.value_hint);
        }
        out << L"\n    " << spec.description;
        if (!spec.default_text.empty()) {
            out << L" (default: " << spec.default_text << L")";
        }
        out << L"\n";
    }
    return out.str();
}

inline bool split_option_token(
    const std::wstring& arg,
    std::wstring* name,
    std::optional<std::wstring>* inline_value) {
    const std::size_t pos = arg.find(L'=');
    if (pos == std::wstring::npos) {
        *name = arg;
        *inline_value = std::nullopt;
        return true;
    }

    *name = arg.substr(0, pos);
    *inline_value = arg.substr(pos + 1);
    return true;
}

inline ParseResult parse_args(
    int argc,
    wchar_t* argv[],
    const OptionRegistry& registry = default_option_registry()) {
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

        if (!result.options.target_input.has_value() &&
            ((!arg.empty() && arg.front() == L'-') || arg == L"/?")) {
            std::wstring option_name;
            std::optional<std::wstring> inline_value;
            split_option_token(arg, &option_name, &inline_value);

            const OptionSpec* spec = registry.find(option_name);
            if (spec == nullptr) {
                result.ok = false;
                result.error = L"Unknown option: " + option_name;
                return result;
            }

            std::optional<std::wstring> value = inline_value;
            if (spec->takes_value && !value.has_value()) {
                if (i + 1 >= argc) {
                    result.ok = false;
                    result.error = L"Option requires value: " + spec->name;
                    return result;
                }
                ++i;
                value = std::wstring(argv[i]);
            } else if (!spec->takes_value && value.has_value()) {
                result.ok = false;
                result.error = L"Option does not accept value: " + spec->name;
                return result;
            }

            std::wstring error;
            if (!spec->handler(result.options, value, error)) {
                result.ok = false;
                result.error = error.empty() ? (L"Failed to handle option: " + spec->name) : error;
                return result;
            }
            continue;
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
