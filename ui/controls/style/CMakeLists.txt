# MyBench.Style — the Qt Quick Controls style of MYBENCH (Button, ComboBox, CheckBox, RadioButton,
# Switch, Slider, SpinBox, TextField, TextArea, TabBar/TabButton, ToolTip, Menu/MenuItem, Popup,
# Dialog, ScrollBar, SplitView, ...), built on QtQuick.Templates with the MyBench.Controls Theme.
# Unimplemented controls fall back to the Basic style. Selected at runtime by
# QQuickStyle::setStyle("MyBench.Style") (mybench::runShell).

qt_add_library(mybench_style STATIC)

file(GLOB MYBENCH_STYLE_QML CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.qml)

qt_add_qml_module(mybench_style
    URI MyBench.Style
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/MyBench/Style
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    IMPORTS QtQuick.Controls.Basic
    DEPENDENCIES QtQuick QtQuick.Templates MyBench.Controls
    QML_FILES ${MYBENCH_STYLE_QML}
)
target_link_libraries(mybench_style PRIVATE mybench_controls Qt6::Quick)
