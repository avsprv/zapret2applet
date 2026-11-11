find_package(Qt6 ${QZAPRET_QT_MIN} REQUIRED COMPONENTS QuickTest)

qt_add_executable(test_qml_navigation
    main.cpp
)

target_link_libraries(test_qml_navigation PRIVATE
    Qt6::Quick
    Qt6::QuickTest
)

add_test(NAME qml_navigation
    COMMAND test_qml_navigation -input ${CMAKE_CURRENT_SOURCE_DIR}
)

set_tests_properties(qml_navigation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
