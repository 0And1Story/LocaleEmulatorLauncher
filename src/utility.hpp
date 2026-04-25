#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace le::util {

inline std::wstring trim(std::wstring_view value) {
    std::size_t start = 0;
    while (start < value.size()) {
        const wchar_t ch = value[start];
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
            break;
        }
        ++start;
    }

    std::size_t end = value.size();
    while (end > start) {
        const wchar_t ch = value[end - 1];
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
            break;
        }
        --end;
    }

    return std::wstring(value.substr(start, end - start));
}

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

inline bool iequals_ascii(std::wstring_view lhs, std::wstring_view rhs) {
    return to_lower_ascii(lhs) == to_lower_ascii(rhs);
}

inline std::optional<std::wstring> getenv_w(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return std::nullopt;
    }

    std::wstring buffer(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), size);
    if (written == 0) {
        return std::nullopt;
    }
    buffer.resize(written);
    return buffer;
}

inline std::wstring win32_error_message(DWORD error_code) {
    LPWSTR raw = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(
        flags,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&raw),
        0,
        nullptr);

    std::wstring message;
    if (len != 0 && raw != nullptr) {
        message.assign(raw, len);
        LocalFree(raw);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    if (message.empty()) {
        message = L"Unknown error";
    }
    return message;
}

inline std::filesystem::path executable_path() {
    std::wstring buffer(1024, L'\0');

    while (true) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size() - 1) {
            buffer.resize(len);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

inline std::filesystem::path executable_dir() {
    const std::filesystem::path exe = executable_path();
    if (exe.empty()) {
        return {};
    }
    return exe.parent_path();
}

inline std::vector<std::wstring> split_command_line(const std::wstring& command_line) {
    std::vector<std::wstring> args;
    if (command_line.empty()) {
        return args;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(command_line.c_str(), &argc);
    if (argv == nullptr) {
        return args;
    }

    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

inline std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    bool need_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'"') {
            need_quotes = true;
            break;
        }
    }
    if (!need_quotes) {
        return arg;
    }

    std::wstring quoted;
    quoted.push_back(L'"');

    std::size_t backslash_count = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslash_count;
            continue;
        }

        if (ch == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        quoted.append(backslash_count, L'\\');
        backslash_count = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslash_count * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

inline std::wstring join_command_line(const std::vector<std::wstring>& args) {
    std::wstring out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out.push_back(L' ');
        }
        out.append(quote_windows_arg(args[i]));
    }
    return out;
}

inline std::wstring bytes_to_wstring(const std::string& bytes, UINT code_page) {
    if (bytes.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()), result.data(), required);
    if (written <= 0) {
        return {};
    }
    return result;
}

inline std::string wstring_to_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string bytes(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), bytes.data(), required, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    return bytes;
}

inline std::wstring read_file_text(const std::filesystem::path& path, std::wstring* error = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) {
            *error = L"Cannot open file: " + path.wstring();
        }
        return {};
    }

    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return {};
    }

    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return bytes_to_wstring(bytes.substr(3), CP_UTF8);
    }

    std::wstring utf8 = bytes_to_wstring(bytes, CP_UTF8);
    if (!utf8.empty()) {
        return utf8;
    }
    return bytes_to_wstring(bytes, CP_ACP);
}

inline bool write_file_utf8(const std::filesystem::path& path, const std::wstring& text, std::wstring* error = nullptr) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) {
            *error = L"Cannot open file for writing: " + path.wstring();
        }
        return false;
    }

    const std::string bytes = wstring_to_utf8(text);
    out.write("\xEF\xBB\xBF", 3);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        if (error != nullptr) {
            *error = L"Failed to write file: " + path.wstring();
        }
        return false;
    }
    return true;
}

inline std::wstring unescape_xml(std::wstring value) {
    auto replace_all = [](std::wstring& text, std::wstring_view from, std::wstring_view to) {
        std::size_t pos = 0;
        while ((pos = text.find(from.data(), pos, from.size())) != std::wstring::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replace_all(value, L"&amp;", L"&");
    replace_all(value, L"&lt;", L"<");
    replace_all(value, L"&gt;", L">");
    replace_all(value, L"&quot;", L"\"");
    replace_all(value, L"&apos;", L"'");
    return value;
}

inline bool has_console_window() {
    return GetConsoleWindow() != nullptr;
}

inline bool is_valid_handle(HANDLE handle) {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

inline bool is_console_handle(HANDLE handle) {
    if (!is_valid_handle(handle)) {
        return false;
    }
    DWORD mode = 0;
    return GetConsoleMode(handle, &mode) != 0;
}

inline void configure_console_code_page_utf8() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

inline HANDLE open_console_input_handle() {
    static HANDLE handle = INVALID_HANDLE_VALUE;
    if (is_valid_handle(handle)) {
        return handle;
    }

    handle = CreateFileW(
        L"CONIN$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    return handle;
}

inline HANDLE open_console_output_handle() {
    static HANDLE handle = INVALID_HANDLE_VALUE;
    if (is_valid_handle(handle)) {
        return handle;
    }

    handle = CreateFileW(
        L"CONOUT$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    return handle;
}

inline HANDLE effective_stdout_handle() {
    const HANDLE console = open_console_output_handle();
    if (is_valid_handle(console)) {
        return console;
    }
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

inline HANDLE effective_stderr_handle() {
    const HANDLE console = open_console_output_handle();
    if (is_valid_handle(console)) {
        return console;
    }
    return GetStdHandle(STD_ERROR_HANDLE);
}

inline HANDLE effective_stdin_handle() {
    const HANDLE console = open_console_input_handle();
    if (is_valid_handle(console)) {
        return console;
    }
    return GetStdHandle(STD_INPUT_HANDLE);
}

inline bool write_to_handle(HANDLE handle, std::wstring_view text) {
    if (!is_valid_handle(handle)) {
        return false;
    }

    if (is_console_handle(handle)) {
        DWORD written = 0;
        return WriteConsoleW(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
    }

    const std::wstring tmp(text);
    const std::string utf8 = wstring_to_utf8(tmp);
    DWORD written = 0;
    return WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != 0;
}

inline bool write_stdout(std::wstring_view text) {
    return write_to_handle(effective_stdout_handle(), text);
}

inline bool write_stderr(std::wstring_view text) {
    return write_to_handle(effective_stderr_handle(), text);
}

inline bool read_console_line(std::wstring* out_line) {
    if (out_line == nullptr) {
        return false;
    }
    out_line->clear();

    const HANDLE in = effective_stdin_handle();
    if (is_console_handle(in)) {
        while (true) {
            wchar_t chunk[128] = {};
            DWORD read = 0;
            if (!ReadConsoleW(in, chunk, static_cast<DWORD>(std::size(chunk) - 1), &read, nullptr)) {
                return false;
            }
            if (read == 0) {
                return false;
            }

            out_line->append(chunk, chunk + read);
            const std::size_t line_end = out_line->find_first_of(L"\r\n");
            if (line_end != std::wstring::npos) {
                out_line->resize(line_end);
                while (!out_line->empty() && out_line->back() == L'\r') {
                    out_line->pop_back();
                }
                return true;
            }
        }
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }

    std::wstring text = bytes_to_wstring(line, CP_UTF8);
    if (text.empty() && !line.empty()) {
        text = bytes_to_wstring(line, CP_ACP);
    }
    *out_line = std::move(text);
    return true;
}

inline bool try_attach_parent_console() {
    if (has_console_window()) {
        configure_console_code_page_utf8();
        return true;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return false;
    }
    configure_console_code_page_utf8();
    return true;
}

inline bool ensure_interactive_console() {
    if (has_console_window()) {
        configure_console_code_page_utf8();
        return true;
    }

    // GUI subsystem process should use its own interactive console.
    // Attaching parent console causes cmd prompt interleaving while cmd does not wait for GUI apps.
    if (AllocConsole()) {
        configure_console_code_page_utf8();
        return true;
    }

    // Fallback for environments where a console already exists but this process is detached.
    if (try_attach_parent_console()) {
        return true;
    }
    return false;
}

} // namespace le::util
