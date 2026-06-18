cmake_minimum_required(VERSION 3.16)
project(cutiewaydroid VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Include necessary modules
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Quick)
qt_standard_project_setup()

# Source files for the library and plugin
set(lib_SOURCES
    src/cutiewaydroid.h
    src/cutiewaydroid.cpp)

set(plugin_SOURCES
    src/plugin.cpp src/plugin.h)

# Create the library
qt6_add_library(cutiewaydroid
    SHARED ${lib_SOURCES}
)

# Create the QML module
qt6_add_qml_module(qmlcutiewaydroidplugin
    URI Cutie.Waydroid
    VERSION ${PROJECT_VERSION}
    PLUGIN_TARGET qmlcutiewaydroidplugin
    NO_GENERATE_PLUGIN_SOURCE
    SOURCES
        ${plugin_SOURCES}
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/Cutie/Waydroid
)

# Set target properties
set_target_properties(cutiewaydroid PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER src/cutiewaydroid.h
)

# Link libraries
target_link_libraries(cutiewaydroid PUBLIC
    Qt::Core
    Qt::Quick
)

target_link_libraries(qmlcutiewaydroidplugin PUBLIC
    Qt::Core
    Qt::Quick
    cutiewaydroid
)

# Configure the pkg-config file
configure_file(src/cutiewaydroid.pc.in cutiewaydroid.pc @ONLY)

# Installation instructions
install(TARGETS cutiewaydroid
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(FILES ${CMAKE_BINARY_DIR}/cutiewaydroid.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

install(TARGETS qmlcutiewaydroidplugin
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/qml/Cutie/Waydroid"
    BUNDLE DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/qml/Cutie/Waydroid"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/qml/Cutie/Waydroid"
)

install(DIRECTORY src/qml/Cutie/Waydroid DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/qml/Cutie")
