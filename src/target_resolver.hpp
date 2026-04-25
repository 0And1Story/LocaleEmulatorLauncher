#pragma once

#include "types.hpp"
#include "utility.hpp"

#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace le::target {

inline bool is_lnk_file(const std::filesystem::path& path) {
    return util::iequals_ascii(path.extension().wstring(), L".lnk");
}

inline bool is_exe_file(const std::filesystem::path& path) {
    return util::iequals_ascii(path.extension().wstring(), L".exe");
}

inline std::optional<TargetSpec> resolve_from_shortcut(
    const std::filesystem::path& shortcut_path,
    const std::vector<std::wstring>& cli_args,
    std::wstring* error = nullptr) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_uninit = SUCCEEDED(hr);

    Microsoft::WRL::ComPtr<IShellLinkW> shell_link;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell_link.GetAddressOf()));
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        if (error != nullptr) {
            *error = L"CoCreateInstance(CLSID_ShellLink) failed.";
        }
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IPersistFile> persist_file;
    hr = shell_link.As(&persist_file);
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        if (error != nullptr) {
            *error = L"Query IPersistFile from IShellLink failed.";
        }
        return std::nullopt;
    }

    hr = persist_file->Load(shortcut_path.c_str(), STGM_READ);
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        if (error != nullptr) {
            *error = L"Failed to load shortcut: " + shortcut_path.wstring();
        }
        return std::nullopt;
    }

    std::wstring target_buffer(32768, L'\0');
    WIN32_FIND_DATAW find_data{};
    hr = shell_link->GetPath(target_buffer.data(), static_cast<int>(target_buffer.size()), &find_data, SLGP_RAWPATH);
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        if (error != nullptr) {
            *error = L"Failed to read target from shortcut: " + shortcut_path.wstring();
        }
        return std::nullopt;
    }
    target_buffer.resize(wcsnlen(target_buffer.c_str(), target_buffer.size()));
    if (target_buffer.empty()) {
        if (need_uninit) {
            CoUninitialize();
        }
        if (error != nullptr) {
            *error = L"Shortcut target path is empty: " + shortcut_path.wstring();
        }
        return std::nullopt;
    }

    std::wstring work_dir_buffer(32768, L'\0');
    hr = shell_link->GetWorkingDirectory(work_dir_buffer.data(), static_cast<int>(work_dir_buffer.size()));
    if (FAILED(hr)) {
        work_dir_buffer.clear();
    } else {
        work_dir_buffer.resize(wcsnlen(work_dir_buffer.c_str(), work_dir_buffer.size()));
    }

    std::wstring args_buffer(32768, L'\0');
    hr = shell_link->GetArguments(args_buffer.data(), static_cast<int>(args_buffer.size()));
    if (FAILED(hr)) {
        args_buffer.clear();
    } else {
        args_buffer.resize(wcsnlen(args_buffer.c_str(), args_buffer.size()));
    }

    if (need_uninit) {
        CoUninitialize();
    }

    std::filesystem::path actual_target(target_buffer);
    if (actual_target.is_relative()) {
        actual_target = std::filesystem::absolute(shortcut_path.parent_path() / actual_target);
    } else {
        actual_target = std::filesystem::absolute(actual_target);
    }

    std::filesystem::path working_dir;
    if (!work_dir_buffer.empty()) {
        working_dir = std::filesystem::path(work_dir_buffer);
        if (working_dir.is_relative()) {
            working_dir = std::filesystem::absolute(shortcut_path.parent_path() / working_dir);
        }
    } else {
        working_dir = actual_target.parent_path();
    }

    std::vector<std::wstring> launch_args = util::split_command_line(args_buffer);
    launch_args.insert(launch_args.end(), cli_args.begin(), cli_args.end());

    return TargetSpec{
        .original_input = std::filesystem::absolute(shortcut_path),
        .actual_path = std::move(actual_target),
        .working_dir = std::move(working_dir),
        .launch_args = std::move(launch_args),
    };
}

inline std::optional<TargetSpec> resolve_target(
    const std::filesystem::path& input,
    const std::vector<std::wstring>& cli_args,
    std::wstring* error = nullptr) {
    if (input.empty()) {
        if (error != nullptr) {
            *error = L"Target path is empty.";
        }
        return std::nullopt;
    }

    const std::filesystem::path absolute_input = std::filesystem::absolute(input);
    if (!std::filesystem::exists(absolute_input)) {
        if (error != nullptr) {
            *error = L"Target not found: " + absolute_input.wstring();
        }
        return std::nullopt;
    }

    if (is_lnk_file(absolute_input)) {
        return resolve_from_shortcut(absolute_input, cli_args, error);
    }

    if (!is_exe_file(absolute_input)) {
        if (error != nullptr) {
            *error = L"Unsupported target extension, expected .exe or .lnk: " + absolute_input.wstring();
        }
        return std::nullopt;
    }

    return TargetSpec{
        .original_input = absolute_input,
        .actual_path = absolute_input,
        .working_dir = absolute_input.parent_path(),
        .launch_args = cli_args,
    };
}

} // namespace le::target
