# Locale Emulator Launcher

一个Locale Emulator的简易启动器，提供命令行接口来启动目标程序，并统一管理Locale Emulator的配置与参数。

## 项目目的

Locale Emulator是一个强大的工具，但其启动参数较为复杂，且每次启动都需要在右键菜单中手动选择，对于需要频繁启动的程序来说较为麻烦。如果要配置快捷方式，其命令行参数门槛较高，且不够直观。**有没有什么方法可以一劳永逸呢？有的兄弟，有的。**

这个项目旨在方便地设置和记录Locale Emulator的启动参数，尤其适用于需要通过快捷方式启动的场景。通过一个轻量级的命令行工具，用户可以快速启动目标程序，并且不需要每次都手动配置Locale Emulator。

- 提供简洁的命令行入口来启动 `.exe` / `.lnk` 目标程序。
- 统一管理 Locale Emulator 启动参数与配置优先级。
- 在非交互场景下静默启动；需要交互时提供配置向导与 Profile 选择。

## 特色功能

- 支持 LEProc 所有模式：`path | run | runas | manage | global`
- 支持 `.lnk` 解析（目标路径、工作目录、快捷方式参数）
- 配置优先级：内置默认 < `config.ini` < `leconfig.ini` < 命令行参数
- 自动发现安装路径：`--lepath` 未提供时，按 `PATH` 与常见安装目录查找 `LEProc.exe`
- `runas` 模式缺失 `ProfileGuid` 时，可从 `LEConfig.xml` 读取并数字选择 Profile

## 快速开始

1. 下载预编译版本或自行构建。[[GitHub Releases]](https://github.com/0And1Story/LocaleEmulatorLauncher/releases/latest)
2. 将 `LocaleEmulator.exe` 放在任意目录。
3. 将想要启动的目标程序路径（`.exe` 或 `.lnk`）拖到 `LocaleEmulator.exe` 上即可直接启动。
  - 可能会弹出一个窗口选择 Profile（如果需要），选择后配置会记录下来，后续不需要再选择。
  - 如果不想每次都选择，可以通过在启动器目录使用--config参数进入交互配置向导，提前写入默认配置。
4. 命令行等高级用法请参考下文。

## 使用方式

### 基本用法

```powershell
LocaleEmulator.exe [options] <target.exe|target.lnk> [target args...]
```

### 命令行参数

| 参数 | 说明 |
|---|---|
| `--help`, `-h`, `/?` | 显示帮助 |
| `--config` | 进入交互配置向导 |
| `--lepath <path>` | 指定 Locale Emulator 安装目录或 `LEProc.exe` 路径 |
| `--profile <guid>` | 指定 `runas` 模式使用的 Profile GUID |
| `--mode <mode>` | `path | run | runas | manage | global`（默认 `runas`） |

### 示例

```powershell
# 默认 runas（若未提供 profile，会交互选择）
LocaleEmulator.exe "D:\Games\xxx\game.exe"

# 指定模式与安装路径
LocaleEmulator.exe --mode run --lepath "D:\Program Files\LocaleEmulator" "D:\Games\xxx\game.exe"

# 直接指定 profile
LocaleEmulator.exe --mode runas --profile "338da870-a74e-405c-bf1b-3726060dcc01" "D:\Games\xxx\game.exe"
```

## 配置文件

**文件位置：**
- 默认配置：`<启动器目录>/config.ini`
- 程序单独配置：`<目标程序目录>/leconfig.ini`

运行时读取优先级（从高到低）：

1. 命令行参数
2. 目标目录 `leconfig.ini`
3. 启动器目录 `config.ini`
4. 内置默认

**配置文件格式（INI）：**

```ini
InstallPath=D:\Program Files\LocaleEmulator
ProfileGuid=338da870-a74e-405c-bf1b-3726060dcc01
Mode=runas
```

**可以通过--config参数快捷写入配置。**

- 在**启动器目录**执行：写入当前目录 `config.ini`
- 在**非启动器目录**执行：写入当前目录 `leconfig.ini`

`runas` 模式下，向导会优先尝试读取 `LEConfig.xml` 并提供 Profile 数字选择列表。

## 构建方法

### 环境要求

- Windows
- CMake >= 3.20
- Visual Studio 2022（MSVC，支持 C++23）

### 构建命令

```powershell
cmake -S . -B build
cmake --build build --config Release
```

产物：

- `build\Release\LocaleEmulator.exe`
