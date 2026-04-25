#include "utility.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>

namespace {

bool has_supported_extension(const std::filesystem::path& path) {
    const std::wstring ext = le::util::to_lower_ascii(path.extension().wstring());
    return ext == L".exe" || ext == L".lnk";
}

std::wstring sanitize_filename(std::wstring name) {
    if (name.empty()) {
        return L"Shortcut";
    }

    for (wchar_t& ch : name) {
        switch (ch) {
        case L'\\':
        case L'/':
        case L':':
        case L'*':
        case L'?':
        case L'"':
        case L'<':
        case L'>':
        case L'|':
            ch = L'_';
            break;
        default:
            break;
        }
    }
    return name;
}

std::filesystem::path make_shortcut_name_on_desktop(
    const std::filesystem::path& desktop_dir,
    const std::wstring& target_display_name) {
    const std::wstring base_name = sanitize_filename(target_display_name);
    std::filesystem::path candidate = desktop_dir / (base_name + L".lnk");
    if (!std::filesystem::exists(candidate)) {
        return candidate;
    }

    for (int i = 2; i < 1000; ++i) {
        candidate = desktop_dir / (base_name + L" (" + std::to_wstring(i) + L").lnk");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return desktop_dir / (base_name + L" (" + std::to_wstring(GetTickCount64()) + L").lnk");
}

std::optional<std::filesystem::path> resolve_lnk_target_path(const std::filesystem::path& lnk_path) {
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()));
    if (FAILED(hr)) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IPersistFile> persist;
    hr = link.As(&persist);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    hr = persist->Load(lnk_path.c_str(), STGM_READ);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    std::wstring target_buffer(32768, L'\0');
    WIN32_FIND_DATAW find_data{};
    hr = link->GetPath(target_buffer.data(), static_cast<int>(target_buffer.size()), &find_data, SLGP_RAWPATH);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    target_buffer.resize(wcsnlen(target_buffer.c_str(), target_buffer.size()));
    if (target_buffer.empty()) {
        return std::nullopt;
    }

    std::filesystem::path target_path(target_buffer);
    if (target_path.is_relative()) {
        target_path = std::filesystem::absolute(lnk_path.parent_path() / target_path);
    } else {
        target_path = std::filesystem::absolute(target_path);
    }

    return target_path;
}

std::optional<std::filesystem::path> get_desktop_path() {
    PWSTR raw_path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(hr) || raw_path == nullptr) {
        return std::nullopt;
    }

    const std::filesystem::path desktop_path(raw_path);
    CoTaskMemFree(raw_path);
    return desktop_path;
}

int run_create_shortcut(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        return 2;
    }

    const std::filesystem::path input_path = std::filesystem::absolute(std::filesystem::path(argv[1]));
    if (!std::filesystem::exists(input_path)) {
        return 3;
    }
    if (!has_supported_extension(input_path)) {
        return 4;
    }

    const std::filesystem::path launcher_path = le::util::executable_dir() / L"LocaleEmulator.exe";
    if (!std::filesystem::exists(launcher_path)) {
        return 5;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_uninit = SUCCEEDED(hr);
    if (FAILED(hr)) {
        return 6;
    }

    std::filesystem::path target_program_for_metadata = input_path;
    if (le::util::iequals_ascii(input_path.extension().wstring(), L".lnk")) {
        const std::optional<std::filesystem::path> lnk_target = resolve_lnk_target_path(input_path);
        if (lnk_target.has_value() && !lnk_target->empty()) {
            target_program_for_metadata = *lnk_target;
        }
    }

    const std::wstring target_display_name =
        sanitize_filename(target_program_for_metadata.stem().wstring().empty()
            ? input_path.stem().wstring()
            : target_program_for_metadata.stem().wstring());

    const std::optional<std::filesystem::path> desktop_dir = get_desktop_path();
    if (!desktop_dir.has_value()) {
        if (need_uninit) {
            CoUninitialize();
        }
        return 7;
    }

    const std::filesystem::path shortcut_path =
        make_shortcut_name_on_desktop(*desktop_dir, target_display_name);
    const std::wstring shortcut_args = le::util::quote_windows_arg(input_path.wstring());

    Microsoft::WRL::ComPtr<IShellLinkW> shell_link;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell_link.GetAddressOf()));
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        return 8;
    }

    hr = shell_link->SetPath(launcher_path.c_str());
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        return 9;
    }

    hr = shell_link->SetArguments(shortcut_args.c_str());
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        return 10;
    }

    hr = shell_link->SetWorkingDirectory(target_program_for_metadata.parent_path().wstring().c_str());
    if (FAILED(hr)) {
        hr = shell_link->SetWorkingDirectory(launcher_path.parent_path().wstring().c_str());
        if (FAILED(hr)) {
            if (need_uninit) {
                CoUninitialize();
            }
            return 11;
        }
    }

    if (!target_program_for_metadata.empty() && std::filesystem::exists(target_program_for_metadata)) {
        shell_link->SetIconLocation(target_program_for_metadata.c_str(), 0);
    } else {
        shell_link->SetIconLocation(input_path.c_str(), 0);
    }

    Microsoft::WRL::ComPtr<IPersistFile> persist_file;
    hr = shell_link.As(&persist_file);
    if (FAILED(hr)) {
        if (need_uninit) {
            CoUninitialize();
        }
        return 12;
    }

    hr = persist_file->Save(shortcut_path.c_str(), TRUE);
    if (need_uninit) {
        CoUninitialize();
    }
    if (FAILED(hr)) {
        return 13;
    }

    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr || argc <= 0) {
        return 1;
    }

    const int exit_code = run_create_shortcut(argc, argv);
    LocalFree(argv);
    return exit_code;
}
