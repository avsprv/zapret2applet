find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS QuickTest)

qt_add_executable(test_qml_navigation
    main.cpp
)

target_compile_definitions(test_qml_navigation PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(test_qml_navigation PRIVATE
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickTest
)

add_test(NAME qml_navigation COMMAND test_qml_navigation)

set_tests_properties(qml_navigation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
