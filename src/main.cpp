#include "arg_parser.hpp"
#include "cli.hpp"
#include "config.hpp"
#include "launcher.hpp"
#include "le_profile.hpp"
#include "target_resolver.hpp"
#include "types.hpp"
#include "utility.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <optional>
#include <string>

namespace {

bool print_error(const std::wstring& text) {
    if (!le::util::try_attach_parent_console()) {
        return false;
    }
    return le::util::write_stderr(text);
}

bool print_info(const std::wstring& text) {
    if (!le::util::try_attach_parent_console()) {
        return false;
    }
    return le::util::write_stdout(text);
}

void print_error_and_help(const std::wstring& message) {
    if (!le::util::try_attach_parent_console()) {
        return;
    }
    le::util::write_stderr(L"Error: " + message + L"\n\n");
    le::util::write_stderr(le::usage_text());
}

int run_launcher(int argc, wchar_t* argv[]) {
    const le::ParseResult parse_result = le::parse_args(argc, argv);
    if (!parse_result.ok) {
        print_error_and_help(parse_result.error);
        return 2;
    }

    const le::CliOptions& cli_options = parse_result.options;
    if (cli_options.show_help || argc <= 1) {
        print_info(le::usage_text());
        return 0;
    }

    auto run_config_wizard_for_file = [&](const std::filesystem::path& ini_path, le::RuntimeConfig defaults) -> bool {
        if (!le::util::ensure_interactive_console()) {
            print_error(L"Failed to open interactive console.\n");
            return false;
        }

        if (!defaults.install_path.has_value()) {
            le::RuntimeConfig probe = defaults;
            std::wstring ignored_diagnostic;
            const auto probe_path = le::config::resolve_leproc_exe(probe, &ignored_diagnostic);
            if (probe_path.has_value() && probe.install_path.has_value()) {
                defaults.install_path = probe.install_path;
            }
        }

        std::wstring wizard_error;
        if (!le::cli::run_config_wizard(ini_path, defaults, &wizard_error)) {
            le::util::write_stderr(L"Config wizard failed: " + wizard_error + L"\n");
            return false;
        }
        return true;
    };

    if (cli_options.run_config_wizard) {
        const std::filesystem::path current_dir = std::filesystem::current_path();
        const std::filesystem::path launcher_dir_for_config = le::util::executable_dir();

        std::error_code compare_ec;
        const bool in_launcher_dir =
            !launcher_dir_for_config.empty() &&
            std::filesystem::equivalent(current_dir, launcher_dir_for_config, compare_ec);

        const std::filesystem::path output_ini =
            current_dir / (in_launcher_dir ? L"config.ini" : L"leconfig.ini");

        std::wstring warnings;
        le::RuntimeConfig defaults = le::config::load_runtime_from_ini_file(output_ini, &warnings);
        if (cli_options.install_path.has_value()) {
            defaults.install_path = cli_options.install_path;
        }
        if (cli_options.mode.has_value()) {
            defaults.mode = *cli_options.mode;
        }
        if (cli_options.profile_guid.has_value()) {
            defaults.profile_guid = cli_options.profile_guid;
        }

        if (!warnings.empty()) {
            le::util::write_stdout(L"Warning: " + warnings + L"\n");
        }

        if (!run_config_wizard_for_file(output_ini, defaults)) {
            return 1;
        }
        return 0;
    }

    std::optional<le::TargetSpec> target_spec;
    if (cli_options.target_input.has_value()) {
        std::wstring target_error;
        target_spec = le::target::resolve_target(*cli_options.target_input, cli_options.target_passthrough_args, &target_error);
        if (!target_spec.has_value()) {
            print_error(L"Target resolve failed: " + target_error + L"\n");
            return 1;
        }
    }

    const std::optional<std::filesystem::path> target_dir = target_spec.has_value()
        ? std::optional<std::filesystem::path>(target_spec->actual_path.parent_path())
        : std::nullopt;

    const std::filesystem::path launcher_dir = le::util::executable_dir();

    std::wstring warnings;
    le::RuntimeConfig runtime_config = le::config::load_runtime_config(cli_options, launcher_dir, target_dir, nullptr, &warnings);
    if (!warnings.empty()) {
        print_error(L"Warning: " + warnings + L"\n");
    }

    if (runtime_config.mode != le::Mode::Global && !target_spec.has_value()) {
        print_error(L"Missing target path.\n");
        print_info(le::usage_text());
        return 1;
    }

    std::wstring resolve_diagnostic;
    bool had_install_path_before_discovery = runtime_config.install_path.has_value();
    std::optional<std::filesystem::path> leproc_exe = le::config::resolve_leproc_exe(runtime_config, &resolve_diagnostic);
    if (!leproc_exe.has_value() && !had_install_path_before_discovery) {
        print_error(L"InstallPath is not configured and LEProc.exe was not auto-discovered.\n");
        print_error(L"Entering config wizard to generate config.ini...\n");

        le::RuntimeConfig defaults = runtime_config;
        const std::filesystem::path launcher_config_ini = launcher_dir / L"config.ini";
        if (!run_config_wizard_for_file(launcher_config_ini, defaults)) {
            return 1;
        }

        warnings.clear();
        runtime_config = le::config::load_runtime_config(cli_options, launcher_dir, target_dir, nullptr, &warnings);
        if (!warnings.empty()) {
            print_error(L"Warning: " + warnings + L"\n");
        }

        resolve_diagnostic.clear();
        had_install_path_before_discovery = runtime_config.install_path.has_value();
        leproc_exe = le::config::resolve_leproc_exe(runtime_config, &resolve_diagnostic);
    }

    if (!leproc_exe.has_value()) {
        print_error(L"Cannot locate LEProc.exe.\n");
        if (!resolve_diagnostic.empty()) {
            print_error(resolve_diagnostic + L"\n");
        }
        return 1;
    }

    if (le::mode_requires_guid(runtime_config.mode) && !runtime_config.profile_guid.has_value()) {
        if (!le::util::ensure_interactive_console()) {
            return 1;
        }

        const std::filesystem::path xml_path = le::profile::profile_xml_path_from_leproc(*leproc_exe);
        std::wstring profile_error;
        const std::vector<le::ProfileEntry> profiles = le::profile::load_profiles(xml_path, &profile_error);
        if (profiles.empty()) {
            le::util::write_stderr(L"ProfileGuid is required but no profile can be loaded.\n");
            le::util::write_stderr(L"Details: " + profile_error + L"\n");
            le::util::write_stderr(L"Use --profile <guid> to specify manually.\n");
            return 1;
        }

        const std::optional<le::ProfileEntry> selected = le::cli::choose_profile(profiles);
        if (!selected.has_value()) {
            le::util::write_stderr(L"Profile selection cancelled.\n");
            return 1;
        }

        runtime_config.profile_guid = selected->guid;
        const std::filesystem::path persist_ini = le::config::choose_profile_persist_ini(launcher_dir, target_dir);
        std::wstring persist_error;
        if (!le::config::persist_profile_guid(persist_ini, selected->guid, &persist_error)) {
            le::util::write_stderr(L"Warning: failed to persist ProfileGuid: " + persist_error + L"\n");
        }
    }

    std::wstring build_error;
    const std::optional<le::LaunchPlan> launch_plan =
        le::launcher::build_launch_plan(*leproc_exe, runtime_config, target_spec, &build_error);
    if (!launch_plan.has_value()) {
        print_error(L"Build launch command failed: " + build_error + L"\n");
        return 1;
    }

    std::wstring launch_error;
    if (!le::launcher::start_process(*launch_plan, &launch_error)) {
        print_error(L"Launch failed: " + launch_error + L"\n");
        return 1;
    }

    print_info(L"Launched: " + le::launcher::preview_command(*launch_plan) + L"\n");
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr || argc <= 0) {
        return 1;
    }

    const int code = run_launcher(argc, argv);
    LocalFree(argv);
    return code;
}
