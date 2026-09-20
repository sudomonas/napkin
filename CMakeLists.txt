cmake_minimum_required(VERSION 3.24)
project(napkin VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Widgets Network Test)

# --- SQLite -------------------------------------------------------------------
# Napkin links SQLite3::SQLite3, and how that target comes into being is not
# something every environment agrees on. CI found the library and then failed to
# generate, on both platforms:
#
#   Found SQLite3: /usr/include (found version "3.45.1")
#   Target "napkin_core" links to SQLite3::SQLite3 but the target was not found.
#
# CMake's own FindSQLite3 defines the target; vcpkg ships a config package that
# defines unofficial::sqlite3::sqlite3 instead; and a Find module reached
# through another prefix may define neither and only set the variables. So this
# takes whichever it is handed and guarantees the one name the rest of the file
# uses, rather than assuming a shape and failing at generate time.
find_package(SQLite3 QUIET)
if(NOT TARGET SQLite3::SQLite3)
  find_package(unofficial-sqlite3 CONFIG QUIET)
  if(TARGET unofficial::sqlite3::sqlite3)
    add_library(SQLite3::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
  elseif(SQLite3_FOUND OR SQLite3_INCLUDE_DIRS OR SQLite3_LIBRARIES)
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
      IMPORTED_LOCATION "${SQLite3_LIBRARIES}"
      INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}")
  else()
    message(FATAL_ERROR
      "SQLite3 was not found. Install the development package "
      "(libsqlite3-dev on Debian/Ubuntu, sqlite on Arch, vcpkg install sqlite3 on Windows).")
  endif()
endif()

qt_standard_project_setup()

# Warnings, in each compiler's own spelling. These were GCC's only, and MSVC
# reported "Command line error D8021: invalid numeric argument '/Wextra'" — the
# first real portability problem the informational Windows job found, and in the
# build system rather than in the code.
# /utf-8: the sources are UTF-8, and without it MSVC reads them in the system
# code page, so a literal like "日本語" in a test compiles to something else.
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- core ---------------------------------------------------------------------
# Domain and data layers. Links Qt6::Core but deliberately NOT Qt6::Widgets, so
# the "domain has no GUI dependency" rule (SPEC.md §10) is enforced by the build
# rather than by good intentions. Everything here is testable headless.
add_library(napkin_core STATIC
  src/domain/Clock.cpp
  src/domain/BufferService.cpp
  src/domain/Preview.cpp
  src/domain/Links.cpp
  src/domain/Search.cpp
  src/domain/TimeFormat.cpp
  src/data/Database.cpp
  src/data/Statement.cpp
  src/data/Migrations.cpp
  src/data/BufferRepository.cpp
  src/data/ItemRepository.cpp
  src/app/Paths.cpp
)
target_link_libraries(napkin_core PUBLIC Qt6::Core SQLite3::SQLite3)
if(WIN32)
  target_link_libraries(napkin_core PUBLIC advapi32)   # ACLs, in app/Paths.cpp
endif()
target_include_directories(napkin_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- media --------------------------------------------------------------------
# Image handling needs QImage, which lives in Qt6::Gui. Kept in its own library
# so napkin_core stays Core-only: the domain and data layers must not acquire a
# GUI dependency by proximity.
add_library(napkin_media STATIC
  src/media/BlobStore.cpp
  src/media/ImageFormats.cpp
  src/media/BlobGc.cpp
  src/media/Exporter.cpp
  src/media/Thumbnailer.cpp
  src/media/ClipboardContent.cpp
)
target_link_libraries(napkin_media PUBLIC napkin_core Qt6::Gui)

# --- ui -----------------------------------------------------------------------
# A library rather than loose sources in the executable, so the widget tree can
# be driven headlessly by tests instead of only by hand.
qt_add_library(napkin_ui STATIC
  src/ui/MainWindow.cpp
  src/ui/BufferListModel.cpp
  src/ui/BufferListView.cpp
  src/ui/BufferCardDelegate.cpp
  src/ui/ItemCanvas.cpp
  src/ui/ItemCard.cpp
  src/ui/MatchHighlighter.cpp
  src/ui/CardFooter.cpp
  src/ui/LinkChip.cpp
  src/ui/MasonryLayout.cpp
  src/ui/BoardLayout.cpp

  src/ui/Autosave.cpp
  src/ui/Icons.cpp
  src/ui/TrayIcon.cpp
  src/ui/UndoToast.cpp
  src/ui/Lightbox.cpp
  src/ui/SweepDialog.cpp
  src/ui/SettingsDialog.cpp
  src/ui/WelcomeView.cpp
  src/ui/EmptyStateView.cpp
  src/app/SingleInstance.cpp
  src/app/Application.cpp
)
# The icons belong to the UI library, not the executable: WelcomeView draws the
# logo, so anything that uses the UI — including the tests — needs them. Compiled
# into the executable they were invisible to everything else.
qt_add_resources(napkin_ui "icons" PREFIX "/" FILES
  resources/icons/16x16/io.github.sudomonas.Napkin.png
  resources/icons/32x32/io.github.sudomonas.Napkin.png
  resources/icons/48x48/io.github.sudomonas.Napkin.png
  resources/icons/64x64/io.github.sudomonas.Napkin.png
  resources/icons/128x128/io.github.sudomonas.Napkin.png
  resources/icons/256x256/io.github.sudomonas.Napkin.png
  resources/icons/trash-empty-128.png
  resources/icons/trash-empty-256.png
  # Toolbar icons, from Lucide (see resources/icons/lucide/LICENSE).
  resources/icons/lucide/trash-2.svg
  resources/icons/lucide/ellipsis.svg
  resources/icons/lucide/settings.svg
  resources/icons/lucide/plus.svg
)
target_link_libraries(napkin_ui PUBLIC napkin_core napkin_media Qt6::Widgets Qt6::Network)

# --- application --------------------------------------------------------------
qt_add_executable(napkin src/main.cpp)
target_link_libraries(napkin PRIVATE napkin_ui)
if(WIN32)
  # A GUI-subsystem executable, so launching Napkin does not also open a
  # console window, and the icon Explorer and shortcuts show.
  target_sources(napkin PRIVATE resources/windows/napkin.rc)
  set_target_properties(napkin PROPERTIES WIN32_EXECUTABLE ON)
endif()

# --- installation -------------------------------------------------------------
# SPEC's phase plan claimed packaging was wired up in Phase 0 and kept green. It
# was not: there was no desktop entry, no icon and no install rule, while
# main.cpp promised the compositor a desktop file that did not exist.
include(GNUInstallDirs)
install(TARGETS napkin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Everything a desktop installs is named after the application id: AppStream
# and Flathub both treat the id, the .desktop basename and the icon name as one
# identity, and a mismatch means the listing and the launcher are unrelated
# objects as far as the system is concerned.
# Freedesktop only: on Windows the icon is compiled into the executable and
# nothing reads a .desktop file.
set(NAPKIN_APPID io.github.sudomonas.Napkin)
if(UNIX AND NOT APPLE)
  install(FILES resources/${NAPKIN_APPID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES resources/${NAPKIN_APPID}.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES resources/icons/${size}x${size}/${NAPKIN_APPID}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
  endforeach()
endif()
set_target_properties(napkin PROPERTIES MACOSX_BUNDLE OFF)

# --- Phase 0 spike (disposable) -----------------------------------------------
qt_add_executable(napkin_spike EXCLUDE_FROM_ALL src/spike/main.cpp)
target_link_libraries(napkin_spike PRIVATE Qt6::Widgets SQLite3::SQLite3)

# --- tests --------------------------------------------------------------------
enable_testing()
foreach(t test_migrations test_invariants test_buffers test_items test_paths test_preview test_performance test_search test_links)
  qt_add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE napkin_core Qt6::Test)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Export writes real image files, so it needs the blob store from napkin_media.
# Linking napkin_media pulls in Qt6::Gui, which makes QTEST_MAIN construct a
# QGuiApplication — and that aborts on a machine with no display. It passed here
# because this machine has one; CI has none, and reported "Subprocess aborted".
# The offscreen platform is not a GUI-test concern, it is a Qt6::Gui concern.
qt_add_executable(test_export tests/test_export.cpp)
target_link_libraries(test_export PRIVATE napkin_media Qt6::Test)
add_test(NAME test_export COMMAND test_export)
set_tests_properties(test_export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Load-testing tool. Not installed: it exists to fill a throwaway profile.
qt_add_executable(napkin_seed tools/seed_corpus.cpp)
target_link_libraries(napkin_seed PRIVATE napkin_core napkin_media Qt6::Gui)
target_include_directories(napkin_seed PRIVATE src)

qt_add_executable(bench_load tools/bench_load.cpp)
target_link_libraries(bench_load PRIVATE napkin_ui Qt6::Widgets)
target_include_directories(bench_load PRIVATE src)

# GUI tests drive the real widget tree offscreen.
foreach(t test_editing test_sweep test_lifecycle test_images test_multiitem test_review_fixes test_canvas test_theme test_chips test_accessibility test_tray)
  qt_add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE napkin_ui Qt6::Test)
  add_test(NAME ${t} COMMAND ${t})
  set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# Windows' native style ignores the palette, so whether dark mode reaches the
# controls can only be seen on the real platform with the real style. The
# offscreen platform always uses Fusion, which would pass regardless.
qt_add_executable(test_native_style tests/test_native_style.cpp)
target_link_libraries(test_native_style PRIVATE napkin_ui Qt6::Test)
add_test(NAME test_native_style COMMAND test_native_style)
if(NOT WIN32)
  set_tests_properties(test_native_style PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# qt_add_executable can make a GUI-subsystem program on Windows, which has no
# stdout: the first Windows run reported two failing suites and not one line of
# why. Tests and tools are console programs; only napkin itself is a GUI one.
get_directory_property(_targets BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _targets)
  get_target_property(_type ${_t} TYPE)
  if(_type STREQUAL "EXECUTABLE" AND NOT _t STREQUAL "napkin")
    set_target_properties(${_t} PROPERTIES WIN32_EXECUTABLE OFF)
  endif()
endforeach()
