cmake_minimum_required(VERSION 3.21)

project(CompositorQt
    VERSION 0.1.0
    DESCRIPTION "Cross-platform rewrite of Compositor in Qt"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(CompositorQt
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/CanvasView.h
    src/CanvasView.cpp
)

target_link_libraries(CompositorQt PRIVATE Qt6::Widgets)

# Windows 上作为 GUI 程序运行(不弹出控制台窗口)
if(WIN32)
    set_target_properties(CompositorQt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# 基础安装规则(便于 CI 用 windeployqt 打包)
include(GNUInstallDirs)
install(TARGETS CompositorQt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
