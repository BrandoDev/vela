# Esplora file (vela-files): Qt Quick senza Qt Quick Controls, per partire
# in fretta. Riusa pezzi della shell: le app installate ("Apri con"), le
# miniature, i service menu di KDE, le azioni di sistema e il componente
# dei menu del tasto destro (MenuPanel.qml).
find_package(Qt6 6.5 REQUIRED COMPONENTS DBus Gui Network Qml Quick Svg)

qt_standard_project_setup(REQUIRES 6.5)

set(SHELL_SRC ${PROJECT_SOURCE_DIR}/shell/src)

qt_add_executable(vela-files
    src/main.cpp
    src/fileops.cpp
    src/fileops.h
    src/foldermodel.cpp
    src/foldermodel.h
    src/places.cpp
    src/places.h
    ${SHELL_SRC}/appmodel.cpp
    ${SHELL_SRC}/appmodel.h
    ${SHELL_SRC}/filethumbnails.cpp
    ${SHELL_SRC}/filethumbnails.h
    ${SHELL_SRC}/iconprovider.h
    ${SHELL_SRC}/mica.h
    ${SHELL_SRC}/servicemenus.cpp
    ${SHELL_SRC}/servicemenus.h
    ${SHELL_SRC}/systemactions.cpp
    ${SHELL_SRC}/systemactions.h)
target_include_directories(vela-files PRIVATE ${SHELL_SRC})

set(FILES_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/TabPage.qml
    qml/NavPane.qml
    qml/AddressBar.qml
    qml/CommandBar.qml
    qml/DetailsView.qml
    qml/IconView.qml
    qml/HomePage.qml
    qml/ThisPcPage.qml
    qml/StatusBar.qml
    qml/MenuLayer.qml
    qml/Dialog.qml
    qml/ProgressPanel.qml
    qml/ToolButton.qml
    qml/FileIcon.qml
    qml/RenameField.qml
    qml/ScrollBar.qml
    ${PROJECT_SOURCE_DIR}/shell/qml/MenuPanel.qml
    ${PROJECT_SOURCE_DIR}/shell/qml/PanelShadow.qml)

foreach(file IN LISTS FILES_QML_FILES)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vela-files
    URI Vela.Files
    VERSION 1.0
    QML_FILES ${FILES_QML_FILES})

target_compile_options(vela-files PRIVATE -Wall -Wextra)
target_link_libraries(vela-files PRIVATE
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg)

install(TARGETS vela-files RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES vela-files.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
