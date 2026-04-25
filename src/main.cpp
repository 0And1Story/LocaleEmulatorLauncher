#include "arg_parser.hpp"
#include "cli.hpp"
#include "config.hpp"
#include "launcher.hpp"
#include "le_profile.hpp"
#include "target_resolver.hpp"
#include "types.hpp"
#include "utility.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace {

void print_error_and_help(const std::wstring& message) {
    std::wcerr << L"Error: " << message << L"\n\n";
    std::wcerr << le::usage_text();
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    const le::ParseResult parse_result = le::parse_args(argc, argv);
    if (!parse_result.ok) {
        print_error_and_help(parse_result.error);
        return 2;
    }

    const le::CliOptions& cli_options = parse_result.options;
    if (cli_options.show_help || argc <= 1) {
        std::wcout << le::usage_text();
        return 0;
    }

    if (cli_options.run_config_wizard) {
        const std::filesystem::path output_ini = std::filesystem::current_path() / L"config.ini";

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
            std::wcerr << L"Warning: " << warnings << L"\n";
        }

        std::wstring wizard_error;
        if (!le::cli::run_config_wizard(output_ini, defaults, &wizard_error)) {
            std::wcerr << L"Config wizard failed: " << wizard_error << L"\n";
            return 1;
        }
        return 0;
    }

    std::optional<le::TargetSpec> target_spec;
    if (cli_options.target_input.has_value()) {
        std::wstring target_error;
        target_spec = le::target::resolve_target(*cli_options.target_input, cli_options.target_passthrough_args, &target_error);
        if (!target_spec.has_value()) {
            std::wcerr << L"Target resolve failed: " << target_error << L"\n";
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
        std::wcerr << L"Warning: " << warnings << L"\n";
    }

    if (runtime_config.mode != le::Mode::Global && !target_spec.has_value()) {
        std::wcerr << L"Missing target path.\n";
        std::wcerr << le::usage_text();
        return 1;
    }

    std::wstring resolve_diagnostic;
    const std::optional<std::filesystem::path> leproc_exe = le::config::resolve_leproc_exe(runtime_config, &resolve_diagnostic);
    if (!leproc_exe.has_value()) {
        std::wcerr << L"Cannot locate LEProc.exe.\n";
        if (!resolve_diagnostic.empty()) {
            std::wcerr << resolve_diagnostic << L"\n";
        }
        return 1;
    }

    if (le::mode_requires_guid(runtime_config.mode) && !runtime_config.profile_guid.has_value()) {
        const std::filesystem::path xml_path = le::profile::profile_xml_path_from_leproc(*leproc_exe);
        std::wstring profile_error;
        const std::vector<le::ProfileEntry> profiles = le::profile::load_profiles(xml_path, &profile_error);
        if (profiles.empty()) {
            std::wcerr << L"ProfileGuid is required but no profile can be loaded.\n";
            std::wcerr << L"Details: " << profile_error << L"\n";
            std::wcerr << L"Use --profile-guid <guid> to specify manually.\n";
            return 1;
        }

        const std::optional<le::ProfileEntry> selected = le::cli::choose_profile(profiles);
        if (!selected.has_value()) {
            std::wcerr << L"Profile selection cancelled.\n";
            return 1;
        }

        runtime_config.profile_guid = selected->guid;
        const std::filesystem::path persist_ini = le::config::choose_profile_persist_ini(launcher_dir, target_dir);
        std::wstring persist_error;
        if (!le::config::persist_profile_guid(persist_ini, selected->guid, &persist_error)) {
            std::wcerr << L"Warning: failed to persist ProfileGuid: " << persist_error << L"\n";
        }
    }

    std::wstring build_error;
    const std::optional<le::LaunchPlan> launch_plan =
        le::launcher::build_launch_plan(*leproc_exe, runtime_config, target_spec, &build_error);
    if (!launch_plan.has_value()) {
        std::wcerr << L"Build launch command failed: " << build_error << L"\n";
        return 1;
    }

    std::wstring launch_error;
    if (!le::launcher::start_process(*launch_plan, &launch_error)) {
        std::wcerr << L"Launch failed: " << launch_error << L"\n";
        return 1;
    }

    std::wcout << L"Launched: " << le::launcher::preview_command(*launch_plan) << L"\n";
    return 0;
}
