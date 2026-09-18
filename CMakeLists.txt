cmake_minimum_required(VERSION 3.21)

project(DHTInspector
    VERSION 1.0.0.0
    DESCRIPTION "Diagnostic tool for the Kademlia Mainline DHT Network"
    LANGUAGES CXX C  # C for the vendored Monocypher
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

# The QML module is built into a directory named after its URI. That would
# be the build directory's own "DHTInspector", which on platforms where
# executables have no suffix is exactly where the binary goes, so the link
# fails with "cannot open output file DHTInspector: Is a directory". Keep
# the generated modules apart from the binaries.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml_modules")

# Our .qml files live in a subdirectory, so let Qt generate the extra
# qmldir files for it rather than hand-maintaining them.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

option(DHTINSPECTOR_BUILD_TESTS "Build the dhtcore test suite" ON)

add_subdirectory(third_party/monocypher)
add_subdirectory(dhtcore)

if(DHTINSPECTOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

qt_add_executable(DHTInspector
    src/main.cpp
)

# Must be set before qt_add_qml_module sees the file.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(DHTInspector
    URI DHTInspector
    VERSION 1.0
    SOURCES
        src/DhtController.h src/DhtController.cpp
        src/CatalogModels.h src/CatalogModels.cpp
        src/DataStoreModels.h src/DataStoreModels.cpp
        src/NodeListModel.h src/NodeListModel.cpp
        src/ProbeModels.h src/ProbeModels.cpp
        src/SearchModels.h src/SearchModels.cpp
        src/StatusTypes.h
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/LabeledField.qml
        qml/Panel.qml
        qml/Badge.qml
        qml/AddressLink.qml
        qml/ThemedButton.qml
        qml/ThemedSwitch.qml
        qml/ThemedSlider.qml
        qml/ThemedTextField.qml
        qml/ThemedComboBox.qml
        qml/NodeListPanel.qml
        qml/EmptyState.qml
        qml/LineChart.qml
        qml/SearchPage.qml
        qml/SetupPage.qml
        qml/DataStorePage.qml
        qml/GlobalHealthPage.qml
        qml/ProbeNodePage.qml
)

target_include_directories(DHTInspector PRIVATE src)

target_compile_definitions(DHTInspector PRIVATE
    DHTINSPECTOR_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(DHTInspector PRIVATE
    dhtcore
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(DHTInspector PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# ---------------------------------------------------------------------------
# Icons. 16 to 24 come from a four-spoke drawing and 32 and 48 from one
# without the orbit rings, because the full artwork turns to mush below
# about 64; see packaging/linux/dhtinspector-small.svg. Regenerate with
# tools/render-icons; see its README.
# ---------------------------------------------------------------------------
set(DHTINSPECTOR_ICON_SIZES 16 22 24 32 48 64 128 256)

set(icon_files "")
foreach(size IN LISTS DHTINSPECTOR_ICON_SIZES)
    list(APPEND icon_files packaging/linux/icons/${size}x${size}/dhtinspector.png)
endforeach()

# Embedded as well as installed: a window icon set by the application works
# even unpackaged, and on Wayland, where matching a window back to its
# .desktop file is unreliable.
qt_add_resources(DHTInspector "appicons"
    PREFIX "/icons"
    BASE packaging/linux/icons
    FILES ${icon_files}
)

# The Windows version resource. Explorer reads the icon and the Details tab
# from here, and every number in it is substituted from project() above so
# the version lives in exactly one place.
if(WIN32)
    # A backslash path would be an escape sequence to the resource compiler.
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/dhtinspector.ico"
         DHTINSPECTOR_ICON_PATH)

    configure_file(
        packaging/windows/dhtinspector.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/dhtinspector.rc"
        @ONLY
    )
    target_sources(DHTInspector PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/dhtinspector.rc")
endif()

# ---------------------------------------------------------------------------
# Install rules. These exist mainly so `cmake --install` can populate an
# AppDir for the Linux AppImage; see packaging/linux/build-appimage.sh.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS DHTInspector
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/dhtinspector.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )
    # The SVG covers sizes nothing has a PNG for, such as a fractional scale
    # factor; icon lookup prefers an exact match, so the PNGs below are what
    # a panel or dock actually draws.
    install(FILES packaging/linux/dhtinspector.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    )
    foreach(size IN LISTS DHTINSPECTOR_ICON_SIZES)
        install(FILES packaging/linux/icons/${size}x${size}/dhtinspector.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps
        )
    endforeach()
endif()

# Runtime deployment only makes sense against a shared Qt. The static Windows
# build links everything into the executable and needs no deploy step.
if(QT6_IS_SHARED_LIBS_BUILD)
    qt_generate_deploy_qml_app_script(
        TARGET DHTInspector
        OUTPUT_SCRIPT deploy_script
        MACOS_BUNDLE_POST_BUILD
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
    )
    install(SCRIPT ${deploy_script})
endif()
