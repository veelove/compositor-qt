cmake_minimum_required(VERSION 3.21)

project(CompositorQt
    VERSION 0.1.0
    DESCRIPTION "Cross-platform rewrite of Compositor in Qt"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test OpenGL)

qt_standard_project_setup()

# ---- 核心库:文档/图层/渲染/撤销(应用与测试共用) ----
qt_add_library(CompositorCore STATIC
    src/Layer.h src/Layer.cpp
    src/Document.h src/Document.cpp
    src/Renderer.h
    src/PainterRenderer.h src/PainterRenderer.cpp
    src/GpuRenderer.h src/GpuRenderer.cpp
    src/UndoCommands.h src/UndoCommands.cpp
    src/PsdReader.h src/PsdReader.cpp
    src/PsdWriter.h src/PsdWriter.cpp
    src/Adjustments.h src/Adjustments.cpp
    src/Geometry.h src/Geometry.cpp
    src/RawProvider.h src/RawProvider.cpp
    src/VisionProvider.h
)
target_include_directories(CompositorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(CompositorCore PUBLIC Qt6::Widgets Qt6::Gui Qt6::OpenGL)

# ---- 应用 ----
qt_add_executable(CompositorQt
    src/main.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/CanvasView.h src/CanvasView.cpp
)
target_link_libraries(CompositorQt PRIVATE CompositorCore Qt6::Widgets)

# Windows 上作为 GUI 程序运行(不弹出控制台窗口)
if(WIN32)
    set_target_properties(CompositorQt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(GNUInstallDirs)
install(TARGETS CompositorQt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---- 测试 ----
enable_testing()
qt_add_executable(tst_document tests/tst_document.cpp)
target_link_libraries(tst_document PRIVATE CompositorCore Qt6::Test)
# 测试程序必须是 console subsystem(默认),确保 stdout/stderr 在 CI 可见
# CompositorQt 应用上方已设为 WIN32_EXECUTABLE=TRUE(tst_document 不设)
add_test(NAME tst_document COMMAND tst_document)
