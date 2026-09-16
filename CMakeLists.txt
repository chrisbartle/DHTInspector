cmake_minimum_required(VERSION 3.21)

project(DHTInspector
    VERSION 0.1.0
    DESCRIPTION "Diagnostic tool for the mainline BitTorrent DHT"
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
        qml/EmptyState.qml
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
    install(FILES packaging/linux/dhtinspector.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
    )
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
