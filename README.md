# ConnectTool - Qt 版在线联机工具

基于 Steam 网络的 P2P 联机场景工具，现在使用 Qt Quick Controls 2 实现界面，并保留原有的房间/转发逻辑。

## 亮点
- **Qt 界面**：完整的桌面 UI（房间状态、好友邀请、端口配置）。
- **Steam 网络**：依旧基于 Steamworks SDK 进行 P2P 通信、房间邀请、回调处理。
- **TCP 转发**：内置本地 TCP 服务器（默认 8888）并通过 Multiplex 管道透传到远端。
- **单一代码路径**：移除 ImGui/GLFW 依赖，统一使用 Qt + CMake + Nix flake。

## 依赖
- C++17 编译器
- Qt 6（Core / Gui / Qml / Quick / QuickControls2）
- Boost（仅头文件即可）
- Steamworks SDK：请将官方 SDK 解压到 `./steamworks`（或备用目录 `./sdk`），其中应包含 `public/` 与 `redistributable_bin/`
- 已登录的 Steam 客户端

> 提示：`steam_appid.txt` 会在构建时写入 `480`，程序启动也会设置 `SteamAppId=480` 环境变量，方便本地调试。

## 使用 Nix flake 构建
仓库自带 `flake.nix`，使用 nixpkgs `nixos-unstable`：

```bash
# 开发环境（含 Qt、clangd 等）
nix develop

# 构建
nix build

# 运行（结果位于 ./result/bin）
./result/bin/connecttool-qt
```

如果 Steamworks SDK 没有放在仓库根目录，请在构建前将其解压到 `steamworks/` 或在 `flake.nix` 中调整 `STEAMWORKS_PATH_HINT`/`STEAMWORKS_SDK_DIR`。

## 手动构建（无 Nix）
```bash
cmake -B build -S . \
  -DSTEAMWORKS_PATH_HINT=/path/to/steamworks \
  -DSTEAMWORKS_SDK_DIR=/path/to/sdk \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 运行
./build/connecttool-qt
```
确保 Qt6 工具链在 `CMAKE_PREFIX_PATH` 或系统路径中可见。

## 界面与使用
- **主持房间**：点击“主持房间”创建 Steam Lobby，并可在好友列表发送邀请。
- **加入房间**：输入房主的 SteamID64/房间 ID 后点击“加入”。
- **端口转发**：本地端口（默认 0，可在 UI 设置）将被 Multiplex 管理器用于桥接 TCP 流量。
- **房间成员**：实时显示成员、延迟与连接方式（直连/中继）。
- **好友邀请**：筛选好友并发送 Steam 邀请（需当前在 Lobby 中）。

## 文件结构
```
connecttool-qt/
├── src/              # Qt 入口与 Backend
├── qml/              # Qt Quick 界面
├── net/              # TCP 服务器与 Multiplex 管道
├── steam/            # Steamworks 相关逻辑
├── flake.nix         # Nix 构建/开发环境
└── CMakeLists.txt    # Qt + Steamworks 构建脚本
```

## 常见问题
- **找不到 Steamworks SDK**：确认 SDK 已解压到 `steamworks/` 或通过 `-DSTEAMWORKS_PATH_HINT`/`-DSTEAMWORKS_SDK_DIR` 显式指定。
- **好友列表为空**：需要 Steam 在线且允许显示好友；可在界面点击“刷新”。
- **连通性问题**：确保本地端口未被占用，且 Steam 客户端网络正常。
