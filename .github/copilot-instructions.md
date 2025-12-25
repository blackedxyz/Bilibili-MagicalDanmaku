**仓库概览**: 本项目是一个基于 Qt/C++ 的桌面（及部分 Android）直播弹幕与自动化工具，入口位于 `mainwindow/main.cpp`，Qt 项目文件为 `Bilibili-MagicalDanmaku.pro`。

**如何快速构建（开发者）**:
- **Dependency**: 需要安装 Qt（与 `.pro` 中列出的模块一致：`core gui network websockets multimedia sql svg qml quick quickcontrols2`），并保证 `C++17` 支持。
- **本地构建**: 在项目根目录：`qmake Bilibili-MagicalDanmaku.pro && make -j$(nproc)`（或用 Qt Creator 打开 `.pro`）。
- **mac 打包**: 参见 `package/mac_pack.sh`，构建 Release 后执行该脚本（依赖 `macdeployqt`，脚本中使用 `/Applications/Qt/5.14.2/clang_64/bin/macdeployqt`）。
- **Android**: 项目包含 `android/`，使用 Qt 的 Android 工具链或执行 `cd android && ./gradlew assembleDebug`（通常通过 Qt Creator 的 androiddeployqt 自动化）。

**重要路径与职责**:
- `mainwindow/`: UI 入口、窗口和主要界面（`mainwindow/main.cpp`, `mainwindow/mainwindow.*`, `mainwindow/mainwindow.ui`）。
- `services/`: 后端服务实现（直播平台适配、AI、SQL、WebServer 等）。修改业务逻辑优先在这里查找对应 `.cpp/.h`。
- `widgets/`: 可复用的界面组件和对话框。
- `third_party/`: 本地第三方库（brotli、gif、qrencode、interactive_buttons 等）；有些 C 源文件直接被编译进二进制。
- `resources/` 与 `resources/resource.qrc`: Qt 资源、图标与内置文件，打包时会被包含。
- `www/`: 本地 Web 界面（内嵌浏览器/插件），`services/web_server/webserver.cpp` 提供本地 HTTP 接口（默认端口与 README 中描述一致，常见为 5520）。

**构建/配置约定与开关**:
- `.pro` 中使用 `DEFINES` 控制可选特性（例如 `ENABLE_TEXTTOSPEECH`, `ENABLE_HTTP_SERVER`, `ENABLE_LUA`, `ENABLE_WEBENGINE`）。编辑 `.pro` 后需重新运行 `qmake`。
- 使用 `INCLUDEPATH` 将模块源码组织为逻辑分区（例如 `services/live_services/*`），新文件请在 `.pro` 的相应 `SOURCES/HEADERS` 段注册。

**运行时约定与重要配置文件**:
- 运行时配置保存在程序目录下的 `settings.ini` / `heaps.ini`，备份目录为 `backup/`。绿色版通过在安装目录放置 `green_version` 文件启用（详见 README）。
- 日志与调试：若 `mainwindow` 的设置 `debug/logFile` 为 true，则会开启 `qInstallMessageHandler(myMsgOutput)`，日志文件位于程序根目录（参见 README 的“调试日志”）。

**常见任务示例（方便 AI 直接行动）**:
- 修改 UI：更新 `*.ui`（`mainwindow/mainwindow.ui`），在对应 `mainwindow/*.cpp`/`*.h` 同步更改信号/槽绑定，重新运行 `qmake` 并编译。
- 添加第三方 C 库：把源文件放入 `third_party/`，在 `.pro` 的 `SOURCES`/`HEADERS` 中注册，注意链接顺序与 `LIBS`。
- 打开/关闭特性：编辑 `DEFINES`（`.pro`）以启用 `ENABLE_WEBENGINE` 等；修改后 `qmake && make`。

**代码风格与约定要点**:
- 语言/标准：C++17 + Qt 信号/槽 风格。
- 资源管理：UI 相关资源优先放入 `resources/resource.qrc`；图标与静态资源集中在 `resources/` 与 `pictures/`。
- 多平台支持：项目同时包含 Windows、macOS、Android 条件编译（请查看 `.pro` 中的 `win32{}` / `macx{}` / `android{}` 段）。

**给 AI 代理的交互建议**:
- 若修改构建文件（`.pro`、`android/build.gradle`），在 PR 描述中指明本地测试步骤（例如 Qt 版本，是否运行 `macdeployqt`）。
- 修改运行时行为前先查 `README.md` 的相应章节（大量行为/脚本逻辑在 README 中有示例），并在提交中列出回归测试要点（如“开启调试日志，验证 `debug.log` 有对应输出”）。
- 对于 UI 改动，附带截图或 `*.ui` diff，并在 CI/PR 描述中说明需要手动验证的交互（例如 Tray、全屏弹幕展示）。

**不要假设/注意事项**:
- 仓库没有自动化单元测试或 CI 配置（未发现测试目录），对行为变更请在本地完整跑一遍 UI/交互流程。
- 若需要使用 WebEngine、Lua、TextToSpeech 等模块，确认目标机器上安装了对应 Qt 模块或在 `.pro` 中提供替代实现。

如果你希望我把某部分展开成更具体的“修改/提交模版”或添加 PR checklist（例如构建矩阵、手动验证步骤），告诉我想要覆盖的场景，我会迭代更新这份说明。
