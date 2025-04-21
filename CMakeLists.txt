cmake_minimum_required(VERSION 3.16)

project(VTKQMLExtension VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_INSTALL_DIR "D:/Qt/6.9.0/msvc2022_64/lib/cmake")
set(VTK_INSTALL_DIR "D:/DownloadedCode/VTK_INSTALL/lib/cmake/vtk-9.4")

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick PATHS ${QT_INSTALL_DIR} NO_DEFAULT_PATH)
find_package(VTK REQUIRED PATHS ${VTK_INSTALL_DIR} NO_DEFAULT_PATH)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appVTKQMLExtension
    main.cpp
)

qt_add_qml_module(appVTKQMLExtension
    URI VTKQMLExtension
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES VTKRendererItem.h VTKRendererItem.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appVTKQMLExtension PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appVTKQMLExtension
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appVTKQMLExtension
    PRIVATE Qt6::Quick
    ${VTK_LIBRARIES}
)

include(GNUInstallDirs)
install(TARGETS appVTKQMLExtension
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
