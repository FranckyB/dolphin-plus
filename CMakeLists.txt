# SPDX-FileCopyrightText: 2006-2026 KDE Contributors
#
# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.16)

# KDE Application Version, managed by release script
set (RELEASE_SERVICE_VERSION_MAJOR "26")
set (RELEASE_SERVICE_VERSION_MINOR "08")
set (RELEASE_SERVICE_VERSION_MICRO "2")
set (RELEASE_SERVICE_VERSION "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}.${RELEASE_SERVICE_VERSION_MICRO}")
project(DolphinPlus VERSION ${RELEASE_SERVICE_VERSION})

set(QT_MIN_VERSION "6.4.0")
set(KF6_MIN_VERSION "6.23.0")

# use C++20 like KF6 itself
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# ECM setup
find_package(ECM ${KF6_MIN_VERSION} CONFIG REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMGenerateDBusServiceFile)
include(ECMConfiguredInstall)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(FeatureSummary)
include(ECMQtDeclareLoggingCategory)
include(ECMDeprecationSettings)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckFunctionExists)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHIN
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/dolphin_version.h"
)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHINVCS
                  VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/dolphinvcs_version.h"
                  PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/DolphinVcsConfigVersion.cmake"
                  SOVERSION 6
)

ecm_setup_version(${RELEASE_SERVICE_VERSION} VARIABLE_PREFIX DOLPHINPRIVATE
                  SOVERSION 6
)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    Concurrent
    Widgets
    Gui
    DBus
)

if (Qt6Gui_VERSION VERSION_GREATER_EQUAL "6.10.0")
    find_package(Qt6GuiPrivate ${QT_MIN_VERSION} REQUIRED NO_MODULE)
endif()

if (UNIX AND NOT APPLE AND NOT HAIKU)
    set(HAVE_X11 TRUE)
endif()

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    KCMUtils
    NewStuff
    CoreAddons
    I18n
    DBusAddons
    Bookmarks
    Config
    KIO
    Parts
    Solid
    IconThemes
    Completion
    TextWidgets
    Notifications
    Crash
    WindowSystem
    WidgetsAddons
    Codecs
    GuiAddons
    ColorScheme
)

set(HAVE_KUSERFEEDBACK FALSE)

find_package(KF6 ${KF6_MIN_VERSION} OPTIONAL_COMPONENTS
    DocTools
)

find_package(PackageKitQt6)
set_package_properties(PackageKitQt6
        PROPERTIES DESCRIPTION "Software Manager integration"
        TYPE RECOMMENDED
        PURPOSE "Used in the service menu installer"
        )
if(PackageKitQt6_FOUND)
    set(HAVE_PACKAGEKIT TRUE)
endif()

find_package(KF6FileMetaData ${KF6_MIN_VERSION})
set_package_properties(KF6FileMetaData PROPERTIES
                       URL "https://projects.kde.org/kfilemetadata"
                       TYPE REQUIRED
                       PURPOSE "For accessing file metadata labels and filesystem metadata"
                      )

find_package(KF6Baloo ${KF6_MIN_VERSION})
set_package_properties(KF6Baloo PROPERTIES DESCRIPTION "Baloo Core libraries"
                       URL "https://www.kde.org"
                       TYPE OPTIONAL
                       PURPOSE "For adding desktop-wide search and tagging support to dolphin"
                      )

find_package(KF6BalooWidgets 26.08.1)
set_package_properties(KF6BalooWidgets PROPERTIES DESCRIPTION "Baloos Widgets"
                       URL "https://www.kde.org"
                       TYPE OPTIONAL
                      )

if (KF6Baloo_FOUND AND KF6BalooWidgets_FOUND)
    message(STATUS "Baloo packages are found")
    set(HAVE_BALOO TRUE)

    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
        Multimedia
        MultimediaWidgets
    )
else ()
    message(WARNING "Baloo packages not found. They are needed for the metadata features of Dolphin (including the information panel).")
endif()

# TODO: drop HAVE_TERMINAL once we are sure the terminal panel works on Windows too.
if(WIN32)
    set(HAVE_TERMINAL FALSE)
else()
    set(HAVE_TERMINAL TRUE)
endif()

# Compatibility with platforms without native fts (e.g. musl)
check_include_files("sys/types.h;sys/stat.h;fts.h" HAVE_FTS_H)
if(HAVE_FTS_H AND NOT HAIKU)
    check_function_exists(fts_open HAVE_FTS_OPEN)
    if(NOT HAVE_FTS_OPEN)
        check_library_exists(fts fts_open "" HAVE_LIB_FTS)
    endif()
endif()

if(HAVE_LIB_FTS)
    set(FTS_LIB fts)
else()
    set(FTS_LIB "")
endif()

add_subdirectory(src)
add_subdirectory(doc EXCLUDE_FROM_ALL)
add_subdirectory(appiumtests)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(KDEClangFormat)
# add clang-format target
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES *.cpp *.h *.c)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

include(KDEGitCommitHooks)
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)
