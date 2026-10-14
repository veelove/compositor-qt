# Compositor Qt

macOS 图像编辑器 [Compositor](https://github.com/robbietilton/Compositor) 的跨平台重写（Qt / C++）。

> 状态：**文档模型 + 撤销/重做 已就位**。主窗口可新建文档、画布支持缩放/平移与原型画笔（所有改动进撤销栈）。GPU 渲染/RAW/视觉智能尚未接入。
> 完整方案与评估见 [Qt重写Compositor评估报告](../compositor-zh-web%205/Qt重写Compositor评估报告.md)。

## 技术栈

- C++17 + Qt 6（Widgets + Test）
- CMake ≥ 3.21
- 许可：MIT（上游 Compositor 亦为 MIT）

## 构建

### Windows（目标平台）

```bat
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.x\msvc2022_64
cmake --build build
ctest --test-dir build --output-on-failure
```

### macOS

```bash
brew install qt cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### CI

`.github/workflows/build-windows.yml`：装 Qt 6.8 → MSVC 环境 → 构建 → 跑 ctest → `windeployqt` 打包。

## 目录结构

```
.
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── MainWindow.h/.cpp     # 主窗口/菜单/快捷键/状态栏;新建文档
│   ├── CanvasView.h/.cpp    # 画布:缩放/平移 + 原型画笔
│   ├── Document.h/.cpp      # 文档:画布尺寸 + 图层列表 + 活动层
│   ├── Layer.h/.cpp         # 图层:像素(QImage)+ 显示属性
│   ├── Renderer.h           # 渲染抽象(后续可换 QRhi)
│   ├── PainterRenderer.cpp  # QPainter CPU 实现(原型)
│   └── UndoCommands.h/.cpp  # QUndoCommand:Add/RemoveLayer、Paint
├── tests/
│   └── tst_document.cpp     # Qt Test:增删图层/撤销/渲染
└── .github/workflows/       # Windows 构建 + 测试 CI
```

## 路线图（对齐评估报告）

- [x] 工程骨架
- [x] 文档模型 + 撤销/重做 + 渲染抽象（PainterRenderer） + 原型画笔  ← 当前
- [ ] CPU 像素算法直译（PSD 解析等）
- [ ] GPU 画布与渲染（QRhiRenderer，替换 PainterRenderer）
- [ ] RAW / 视觉智能（高风险，需先 Spike）
- [ ] 导入导出（PSD/EXR/CMYK）
