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
- [x] 文档模型 + 撤销/重做 + 渲染抽象（PainterRenderer） + 原型画笔
- [x] PSD 解析（PackBits + 图层 + 通道 + 简单 alpha 合成）
- [x] CPU 调色算法：Levels（黑/白场/伽马/输出范围）+ Hue/Saturation
- [x] CPU 调色算法：Brightness/Contrast + Invert
- [x] CPU 调色算法：Curves（保形三次 Hermite 插值 LUT）
- [x] 几何调整：Crop / Resize / Rotate（任意角度）
- [x] CPU 调色算法：Black & White（6 色域权重 + tint 上色）+ Color Balance（阴影/中间调/高光 CMY 偏移）
- [x] CPU 调色算法：Exposure（sRGB↔线性光+EV+offset+gamma LUT，Photo CIE）
- [x] GPU 渲染器：GpuRenderer（OpenGL FBO + 简单 fragment shader + FBO readback），headless 自动 fallback 到 PainterRenderer
- [x] RAW / 视觉智能 Spike：RawProvider.h（LibRaw 选型分析）+ VisionProvider.h（ONNX/OpenCV 选型分析）+ 扩展名识别 stub + StubVisionProvider
- [x] PSD 导出（扁平化 RGB/ARGB + PackBits 写盘 + round-trip 验证）
- [x] 文件 IO 打通：File→Open（PSD/PNG/JPEG）+ Save（PSD/PNG/JPEG）+ 标题栏路径
- [x] 图层树 UI：右侧面板（增/删/上移/下移/显隐/改名/选中），与撤销栈联动
- [x] 滤镜直译：Gaussian Blur（可分离 1D 高斯）/ Motion Blur（角度拖尾）/ Add Noise（确定性 LCG）/ Vignette（径向衰减） + 4 单测
- [x] 滤镜 UI 接入：Filter 菜单（4 项）+ 参数对话框（FiltersUi）+ FilterCommand 撤销命令 + 1 单测
- [x] 画布工具补齐：工具栏（画笔 B/移动 V/吸管 I/选区 M 互斥）+ 图层 offset 渲染 + MoveLayer/ClearRect 撤销命令 + 3 单测
- [x] 调色 GPU 化：AdjustLut 256×1 管线（Levels/Curves/Exposure/BC 一键查表）+ Document renderLut 非破坏性调色 + GpuRenderer 后处理 shader + 7 单测
- [x] 调色 UI 接入：Image 菜单（Levels/Curves/Exposure/BC/HSV/Invert/Reset）+ 参数对话框（AdjustmentsUi）+ AdjustLutCommand 非破坏性撤销命令 + 1 单测
- [x] 多文档标签页：QTabWidget + DocumentTab（Canvas+Document+Path）+ Document 自带 QUndoStack + 切换 tab 同步 Canvas/Layers/Undo 信号 + 修改标记 + Ctrl-W 关闭 + 2 单测
- [x] 滤镜 GPU 化：Gaussian Blur 可分离 box-blur fragment shader（2 pass H/V）+ Document 非破坏性 blur radius + GaussianBlurCommand + Image 菜单"Gaussian Blur (GPU)..." + PainterRenderer CPU fallback + 3 单测
- [x] 滤镜 GPU 化扩展：Vignette + Motion Blur fragment shader + Document 渲染期字段 + Vignette/MotionBlurCommand 撤销 + Image 菜单 2 入口 + PainterRenderer CPU fallback + 3 单测  ← 当前
- [x] 滤镜 GPU 化扩展 2：Add Noise fragment shader（LCG 随机+高斯/单色）+ Document 渲染期字段 + AddNoiseCommand + Image 菜单"Add Noise (GPU)..." + PainterRenderer CPU fallback + 3 单测  ← 当前
- [x] 调色 GPU 化扩展 2：HSV / Black & White / Color Balance 3 个 fragment shader（RGB↔HSL 转换+6 桶权重+CMY 分桶偏移）+ Document 3 字段 + 3 Command + Image 菜单 2 新入口（Hue/Saturation 替换为 GPU 版） + PainterRenderer CPU fallback + 3 单测  ← 当前
- [x] RAW 真实集成：RawDecoder 抽象 + LibRawDecoder（dcraw_emu 子进程桥，零编译依赖）+ StubRawDecoder fallback + MainWindow Open 流程自动检测 RAW + 8 个主流 RAW 扩展名 + 2 单测
- [x] 视觉智能真实集成（ONNX Runtime）：VisionProvider 抽象 + ProcessVisionProvider（Python 桥：stdin=PNG 字节流 + 命令行 x y → stdout mask PNG）+ StubVisionProvider fallback（脚本缺失时给出安装指引） + tools/vision_tool.py（优先尝试 onnxruntime + U²-Net 显著性，按点击点动态阈值 → 退化 OpenCV GrabCut → 退化点击点颜色洪水漫灌，三层降级保证总能出 mask） + CanvasView MagicWand 工具（W 键） + MainWindow 信号接入 + 1 单测（工厂+stub 错误路径）
- [x] Magic Wand 落地到文档：CanvasView 新增 setSelectionMask / setSelectionRect / selectionChanged 公共 API + 自动从 mask 计算 bounding rect + drawSelectionOverlay 渲染 mask 形状（橙红半透明叠加）+ boundingRect dashed line + UndoCommands 新增 MagicWandCommand 复合命令容器 + SelectionCommand（rect-only 与 mask-aware 双路径，可保存 before/after mask 完整还原）+ MainWindow::onMagicWand 串联：mask → ARGB 蒙版图层（橙红 RGB + alpha=mask）→ 复合 push "Magic Wand" 到 QUndoStack（AddLayerCommand + SelectionCommand 一次性撤销）+ CanvasView 移入 CompositorCore 静态库（SelectionCommand 需完整定义 + 单测驱动）+ 3 单测（mask bbox 计算 / SelectionCommand rect-only+mask-aware 双向同步 / 复合命令撤销重做整层还原）
- [x] 按 mask 形状应用变换：新增 Selection 库（sel::maskFromRect / coverage / resampleMask / blendByMask / clearByMask / cropToMask，null mask 一律退化为原矩形行为保证向后兼容）+ UndoCommands 新增 ClearMaskCommand（按 mask 轮廓清除而非矩形）+ CanvasView::clearSelection 检测到 mask 走 ClearMaskCommand、无 mask 走 ClearRectCommand + CanvasView::selectionMaskForLayer 把画布 mask 平移到图层像素坐标系（尊重图层 offset）+ MainWindow::applyFilterToActiveLayer 检测到 mask 时用 sel::blendByMask 把滤镜结果按 mask 混合回原图（选区外零改动，中间值天然支持羽化）+ Image 菜单新增 "Crop to Selection"（Ctrl+Shift+C，按 mask bbox 裁剪图层）+ MainWindow::maskForLayerPixels 共享辅助 + 5 单测（mask 原语 / blendByMask 三态+羽化插值 / clearByMask 形状+null 退化 / cropToMask bbox+交集 / ClearMaskCommand undo-redo）  ← 当前
