# Compositor Qt

macOS 图像编辑器 [Compositor](https://github.com/robbietilton/Compositor) 的跨平台重写（Qt / C++）。

> 状态：**工程骨架**。主窗口、菜单、快捷键、缩放/平移画布已就绪，文档模型与工具尚未接入。
> 完整方案与评估见 [Qt重写Compositor评估报告](../compositor-zh-web%205/Qt重写Compositor评估报告.md)。

## 技术栈

- C++17 + Qt 6（Widgets）
- CMake ≥ 3.21
- 许可：MIT（上游 Compositor 亦为 MIT）

## 构建

### Windows（目标平台）

1. 安装 Qt 6（在线安装器勾选 MSVC 2022 64-bit）或 `aqtinstall`。
2. 安装 Visual Studio 2022（含 C++ 桌面开发）。
3. 配置与编译：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.8.x\msvc2022_64
cmake --build build --config Release
```

### macOS

```bash
brew install qt cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### CI

`.github/workflows/build-windows.yml` 在 Windows runner 上自动安装 Qt 并构建 + `windeployqt` 打包。

## 目录结构

```
.
├── CMakeLists.txt
├── src/
│   ├── main.cpp            # 入口
│   ├── MainWindow.h/.cpp   # 主窗口:菜单/快捷键/状态栏
│   └── CanvasView.h/.cpp   # 画布:缩放/平移/棋盘格占位
└── .github/workflows/      # Windows 构建 CI
```

## 路线图（对齐评估报告）

- [x] 工程骨架（当前）
- [ ] 文档模型 / 历史（撤销重做）
- [ ] CPU 像素算法直译（PSD 解析等）
- [ ] GPU 画布与渲染（QRhi）
- [ ] RAW / 视觉智能（高风险，需先 Spike）
