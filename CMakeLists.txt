cmake_minimum_required(VERSION 3.20)
project(AudioHawk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
option(AUDIOHAWK_DSP_TESTS_ONLY "Build portable DSP tests without PipeWire or UIs" OFF)
if(BUILD_TESTING)
    add_executable(audiohawk_spatial_test tests/spatial_test.c src/core/spatial.c src/core/effects.c)
    target_include_directories(audiohawk_spatial_test PRIVATE include)
    if(NOT MSVC)
        target_link_libraries(audiohawk_spatial_test PRIVATE m)
        target_compile_options(audiohawk_spatial_test PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME spatial COMMAND audiohawk_spatial_test)
endif()
if(AUDIOHAWK_DSP_TESTS_ONLY)
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
find_package(Threads REQUIRED)

option(AUDIOHAWK_BUILD_GTK "Build GTK4/libadwaita UI" ON)
option(AUDIOHAWK_BUILD_QT "Build Qt6 UI" ON)

add_library(audiohawk_core STATIC
    src/core/profiles.c
    src/core/config.c
    src/core/app_profiles.c
    src/core/equalizer.c
    src/core/effects.c
    src/core/spatial.c
    src/core/pipewire_eq.c
    src/core/core.c
)
target_include_directories(audiohawk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PIPEWIRE_INCLUDE_DIRS}
)
target_link_libraries(audiohawk_core PUBLIC ${PIPEWIRE_LIBRARIES} m Threads::Threads)
target_compile_options(audiohawk_core PRIVATE ${PIPEWIRE_CFLAGS_OTHER} -Wall -Wextra)
target_compile_definitions(audiohawk_core PRIVATE _DEFAULT_SOURCE)

# --- Launcher ---
add_executable(audiohawk src/launcher/audiohawk.c)
target_compile_options(audiohawk PRIVATE -Wall -Wextra)

# --- GTK UI ---
set(HAVE_GTK_UI OFF)
if(AUDIOHAWK_BUILD_GTK)
    pkg_check_modules(GTK4 gtk4)
    pkg_check_modules(ADWAITA libadwaita-1)
    if(GTK4_FOUND AND ADWAITA_FOUND)
        set(HAVE_GTK_UI ON)
        add_executable(audiohawk-gtk
            src/gtk/main.c
            src/gtk/dashboard.c
            src/gtk/equalizer_page.c
            src/gtk/settings_page.c
            src/gtk/icons.c
            src/gtk/tray.c
        )
        target_include_directories(audiohawk-gtk PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gtk
            ${GTK4_INCLUDE_DIRS}
            ${ADWAITA_INCLUDE_DIRS}
        )
        target_link_libraries(audiohawk-gtk PRIVATE
            audiohawk_core
            ${GTK4_LIBRARIES}
            ${ADWAITA_LIBRARIES}
        )
        target_compile_options(audiohawk-gtk PRIVATE
            ${GTK4_CFLAGS_OTHER}
            ${ADWAITA_CFLAGS_OTHER}
            -Wall -Wextra
        )
        # Dev + install data roots so icons/CSS resolve even when CWD ≠ repo.
        target_compile_definitions(audiohawk-gtk PRIVATE
            "AUDIOHAWK_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
        )
        # Convenience for running from the build/ directory.
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/data")
        file(CREATE_LINK
            "${CMAKE_SOURCE_DIR}/data/icons"
            "${CMAKE_BINARY_DIR}/data/icons"
            SYMBOLIC)
        file(CREATE_LINK
            "${CMAKE_SOURCE_DIR}/data/css"
            "${CMAKE_BINARY_DIR}/data/css"
            SYMBOLIC)
    else()
        message(WARNING "GTK4/libadwaita not found — skipping audiohawk-gtk. "
                        "Install gtk4-devel libadwaita-devel (Fedora) or "
                        "libgtk-4-dev libadwaita-1-dev (Debian/Ubuntu).")
    endif()
endif()

# --- Qt UI ---
set(HAVE_QT_UI OFF)
if(AUDIOHAWK_BUILD_QT)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(HAVE_QT_UI ON)
        add_executable(audiohawk-qt
            src/qt/main.cpp
            src/qt/Dashboard.cpp
            src/qt/Dashboard.h
        )
        set_target_properties(audiohawk-qt PROPERTIES AUTOMOC ON)
        target_include_directories(audiohawk-qt PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
        )
        target_link_libraries(audiohawk-qt PRIVATE
            audiohawk_core
            Qt6::Widgets
        )
        target_compile_options(audiohawk-qt PRIVATE -Wall -Wextra)
    else()
        message(WARNING "Qt6 Widgets not found — skipping audiohawk-qt.")
    endif()
endif()

# --- Install ---
include(GNUInstallDirs)
install(TARGETS audiohawk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/audiohawk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# App icon for GNOME Overview / KDE Application Launcher / tray theme lookup
install(DIRECTORY data/icons/hicolor/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        FILES_MATCHING
        PATTERN "*.png"
        PATTERN "*.svg")
install(FILES data/appicon/audiohawk.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME audiohawk.svg)
if(HAVE_GTK_UI)
    install(TARGETS audiohawk-gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/css/audiohawk.css DESTINATION ${CMAKE_INSTALL_DATADIR}/audiohawk)
    install(DIRECTORY data/icons/ DESTINATION ${CMAKE_INSTALL_DATADIR}/audiohawk/icons
            FILES_MATCHING PATTERN "*.svg")
    install(DIRECTORY data/appicon/tray/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/audiohawk/tray
            FILES_MATCHING PATTERN "*.png")
endif()
if(HAVE_QT_UI)
    install(TARGETS audiohawk-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/qss/audiohawk.qss DESTINATION ${CMAKE_INSTALL_DATADIR}/audiohawk)
endif()

message(STATUS "AudioHawk UI: GTK=${HAVE_GTK_UI} Qt=${HAVE_QT_UI}")
