#pragma once

#include "types.hpp"
#include "utility.hpp"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace le::launcher {

inline std::optional<LaunchPlan> build_launch_plan(
    const std::filesystem::path& leproc_exe,
    const RuntimeConfig& config,
    const std::optional<TargetSpec>& target,
    std::wstring* error = nullptr) {
    if (leproc_exe.empty() || !std::filesystem::exists(leproc_exe)) {
        if (error != nullptr) {
            *error = L"LEProc.exe not found: " + leproc_exe.wstring();
        }
        return std::nullopt;
    }

    LaunchPlan plan;
    plan.leproc_exe = leproc_exe;
    plan.working_dir = leproc_exe.parent_path();

    auto require_target = [&](const wchar_t* mode_name) -> bool {
        if (target.has_value()) {
            return true;
        }
        if (error != nullptr) {
            *error = std::wstring(L"Mode `") + mode_name + L"` requires target path.";
        }
        return false;
    };

    switch (config.mode) {
    case Mode::Path:
        if (!require_target(L"path")) {
            return std::nullopt;
        }
        plan.arguments.push_back(target->actual_path.wstring());
        plan.arguments.insert(plan.arguments.end(), target->launch_args.begin(), target->launch_args.end());
        plan.working_dir = target->working_dir;
        break;

    case Mode::Run:
        if (!require_target(L"run")) {
            return std::nullopt;
        }
        plan.arguments.push_back(L"-run");
        plan.arguments.push_back(target->actual_path.wstring());
        plan.arguments.insert(plan.arguments.end(), target->launch_args.begin(), target->launch_args.end());
        plan.working_dir = target->working_dir;
        break;

    case Mode::RunAs:
        if (!require_target(L"runas")) {
            return std::nullopt;
        }
        if (!config.profile_guid.has_value() || util::trim(*config.profile_guid).empty()) {
            if (error != nullptr) {
                *error = L"Mode `runas` requires ProfileGuid.";
            }
            return std::nullopt;
        }
        plan.arguments.push_back(L"-runas");
        plan.arguments.push_back(*config.profile_guid);
        plan.arguments.push_back(target->actual_path.wstring());
        plan.arguments.insert(plan.arguments.end(), target->launch_args.begin(), target->launch_args.end());
        plan.working_dir = target->working_dir;
        break;

    case Mode::Manage:
        if (!require_target(L"manage")) {
            return std::nullopt;
        }
        plan.arguments.push_back(L"-manage");
        plan.arguments.push_back(target->actual_path.wstring());
        plan.working_dir = target->working_dir;
        break;

    case Mode::Global:
        plan.arguments.push_back(L"-global");
        break;
    }

    return plan;
}

inline std::wstring preview_command(const LaunchPlan& plan) {
    std::vector<std::wstring> all_args;
    all_args.reserve(plan.arguments.size() + 1);
    all_args.push_back(plan.leproc_exe.wstring());
    all_args.insert(all_args.end(), plan.arguments.begin(), plan.arguments.end());
    return util::join_command_line(all_args);
}

inline bool start_process(const LaunchPlan& plan, std::wstring* error = nullptr) {
    std::vector<std::wstring> full_args;
    full_args.reserve(plan.arguments.size() + 1);
    full_args.push_back(plan.leproc_exe.wstring());
    full_args.insert(full_args.end(), plan.arguments.begin(), plan.arguments.end());

    std::wstring command_line = util::join_command_line(full_args);
    std::vector<wchar_t> mutable_cmd(command_line.begin(), command_line.end());
    mutable_cmd.push_back(L'\0');

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
    if (!util::has_console_window()) {
        creation_flags |= CREATE_NO_WINDOW;
    }

    const BOOL ok = CreateProcessW(
        plan.leproc_exe.c_str(),
        mutable_cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        creation_flags,
        nullptr,
        plan.working_dir.empty() ? nullptr : plan.working_dir.c_str(),
        &startup_info,
        &process_info);

    if (!ok) {
        if (error != nullptr) {
            const DWORD code = GetLastError();
            *error = L"CreateProcessW failed (" + std::to_wstring(code) + L"): " + util::win32_error_message(code);
            *error += L"\nCommand: " + command_line;
        }
        return false;
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
}

} // namespace le::launcher
