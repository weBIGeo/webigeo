#############################################################################
# Alpine Terrain Renderer
# Copyright (C) 2023 Adam Celarek <family name at cg tuwien ac at>
# Copyright (C) 2023 Gerald Kimmersdorfer
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#############################################################################

project(alpine-renderer-app LANGUAGES CXX)

qt_add_executable(alpineapp
    main.cpp
    RenderThreadNotifier.h RenderThreadNotifier.cpp
    TerrainRenderer.h TerrainRenderer.cpp
    HotReloader.h HotReloader.cpp
    AppSettings.h AppSettings.cpp
    timing/TimerFrontendManager.h timing/TimerFrontendManager.cpp
    timing/TimerFrontendObject.h timing/TimerFrontendObject.cpp
)



qt_add_qml_module(alpineapp
    URI Alpine
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        main_loader.qml
        Main.qml
        About.qml
        Map.qml
        SearchBox.qml
        SearchResults.qml
        Settings.qml
        Coordinates.qml
        GeneralSettings.qml
        GlSettings.qml
        FloatingActionButtonGroup.qml
        components/LabledSlider.qml
        components/LabledRangeSlider.qml
        components/ColorPicker.qml
        components/PageDrawer.qml
        components/CheckGroup.qml
        components/SettingsPanel.qml
        components/SettingsTitle.qml
        components/DrawerButton.qml
        components/DrawerSeparator.qml
        components/DrawerSpacer.qml
        components/VectorEditor.qml
        components/DatePicker.qml
        components/DateMonthTablePicker.qml


        components/FloatingActionButton.qml
        components/FluxColor/AngleWheel.qml
        components/FluxColor/ColorChooser.qml
        components/FluxColor/HueRing.qml
        components/FluxColor/HueWheel.qml
        components/FluxColor/WheelArea.qml
    RESOURCES
        icons/favicon.ico
        icons/mascot.jpg
        icons/menu.png
        icons/search.png
        icons/icon.png
        icons/material/monitoring.png
        icons/material/3d_rotation.png
        icons/material/map.png
        icons/material/pin_drop.png
        icons/material/settings.png
        icons/material/info.png
        icons/material/format_paint.png
        icons/material/location_searching.png
        icons/material/my_location.png
        icons/material/navigation.png
        icons/material/navigation_offset.png
        icons/material/add.png
        icons/material/chevron_left.png
        icons/material/visibility_off.png
        icons/presets/basic.png
        icons/presets/shaded.png
        icons/presets/snow.png
        icons/needle_head_down.png
        icons/logo_type_horizontal.png
        icons/logo_type_vertical.png
        icons/logo_type_horizontal_short.png
    SOURCES
        TerrainRendererItem.h TerrainRendererItem.cpp
        GnssInformation.h GnssInformation.cpp
        TrackModel.h TrackModel.cpp
        ModelBinding.h ModelBinding.cpp
)

qt_add_resources(alpineapp "fonts"
    BASE ${alpineapp_fonts_SOURCE_DIR}/
    PREFIX "/fonts"
    FILES
    ${alpineapp_fonts_SOURCE_DIR}/Roboto/Roboto-Regular.ttf
)

qt_add_translations(alpineapp TS_FILES
    i18n/de.ts
    i18n/en.ts
)


set_target_properties(alpineapp PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
)
target_link_libraries(alpineapp PUBLIC gl_engine Qt::Quick Qt::QuickControls2)
if (ALP_ENABLE_DEBUG_GUI)
    message(WARNING "building alpine app with debug gui")
    qt_target_qml_sources(alpineapp QML_FILES StatsWindow.qml)
    target_link_libraries(alpineapp PUBLIC Qt::Charts Qt::Widgets)
    target_compile_definitions(alpineapp PUBLIC ALP_ENABLE_DEBUG_GUI)
endif()
if (ALP_ENABLE_POSITIONING)
    target_link_libraries(alpineapp PUBLIC Qt::Positioning)
    target_compile_definitions(alpineapp PUBLIC "ALP_ENABLE_GNSS")
endif()
if (ALP_ENABLE_APP_SHUTDOWN_AFTER_60S)
    target_compile_definitions(alpineapp PUBLIC "ALP_APP_SHUTDOWN_AFTER_60S")
endif()

if (ANDROID OR EMSCRIPTEN)
    target_compile_definitions(alpineapp PUBLIC ALP_QML_SOURCE_DIR="qrc:///qt/qml/app/")
else()
    target_compile_definitions(alpineapp PUBLIC ALP_QML_SOURCE_DIR="file:/${CMAKE_CURRENT_SOURCE_DIR}/")
endif()

if (ANDROID)
    add_android_openssl_libraries(alpineapp)
    find_package(Qt6 REQUIRED COMPONENTS Widgets) # for QFileDialogue (adding tracks)
    target_link_libraries(alpineapp PUBLIC Qt::Widgets)

    install(TARGETS alpineapp
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if (EMSCRIPTEN)
    target_link_options(alpineapp PUBLIC -sASYNCIFY)

    set(ALP_INSTALL_FILES
        "$<TARGET_FILE_DIR:alpineapp>/alpineapp.js"
        "$<TARGET_FILE_DIR:alpineapp>/alpineapp.wasm"
        "$<TARGET_FILE_DIR:alpineapp>/alpineapp.html"
        "$<TARGET_FILE_DIR:alpineapp>/qtloader.js"
    )

    if (ALP_ENABLE_THREADING)
        list(APPEND ALP_INSTALL_FILES "$<TARGET_FILE_DIR:alpineapp>/alpineapp.worker.js")
    endif()
    install(FILES ${ALP_INSTALL_FILES} DESTINATION ${ALP_WWW_INSTALL_DIR})
endif()
