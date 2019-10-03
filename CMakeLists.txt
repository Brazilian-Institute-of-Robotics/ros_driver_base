# CMakeLists.txt has to be located in the project folder and cmake has to be
# executed from 'project/build' with 'cmake ../'.
cmake_minimum_required(VERSION 2.6)
project(ros_driver_base)

find_package(Boost REQUIRED COMPONENTS thread system)
find_package(catkin REQUIRED COMPONENTS rostime rosconsole)

include_directories(include ${catkin_INCLUDE_DIRS})

catkin_package(
    INCLUDE_DIRS include
    CATKIN_DEPENDS rostime rosconsole
    DEPENDS Boost
    LIBRARIES ${PROJECT_NAME} pthread
)

add_library(ros_driver_base
    src/driver.cpp
    src/bus.cpp
    src/timeout.cpp
    src/io_stream.cpp
    src/exceptions.cpp
    src/tcp_driver.cpp
    src/io_listener.cpp
    src/test_stream.cpp
)

install(TARGETS ros_driver_base
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

install(DIRECTORY include/${PROJECT_NAME}/
    DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
    FILES_MATCHING PATTERN "*.hpp"
    PATTERN "*~" EXCLUDE
)

if(CATKIN_ENABLE_TESTING)
    enable_testing()
    catkin_add_gtest(test_test_stream_gtest test/test_test_stream_gtest.cpp)
    target_link_libraries(test_test_stream_gtest ros_driver_base ${catkin_LIBRARIES})

    # catkin does not have a function/macro for boost unit tests, work around this
    # ideally, we should migrate this to gtest
    find_package(Boost REQUIRED COMPONENTS unit_test_framework system)
    add_executable(test_Driver
        test/suite.cpp
        test/test_driver.cpp
        test/test_test_stream.cpp
    )
    target_compile_definitions(test_Driver PRIVATE BOOST_TEST_DYN_LINK)
    target_link_libraries(test_Driver ros_driver_base ${catkin_LIBRARIES} ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

    add_dependencies(tests test_Driver)
    get_target_property(_target_path test_Driver RUNTIME_OUTPUT_DIRECTORY)

    add_test(NAME test_Driver
        COMMAND "${_target_path}/test_Driver" --log_format=XML
                                              --log_level=all
                                              --log_sink=${CATKIN_TEST_RESULTS_DIR}/${PROJECT_NAME}/boost-test_Driver.xml
        DEPENDENCIES test_Driver
        WORKING_DIRECTORY ${_target_path}
    )
    # end of adding boost test

    add_executable(test_tcp_read test/test_tcp_read.cpp)
    target_link_libraries(test_tcp_read ros_driver_base ${catkin_LIBRARIES})

    add_executable(test_tcp_write test/test_tcp_write.cpp)
    target_link_libraries(test_tcp_write ros_driver_base ${catkin_LIBRARIES})

    add_executable(test_udp_read test/test_udp_read.cpp)
    target_link_libraries(test_udp_read ros_driver_base ${catkin_LIBRARIES})

    add_executable(test_udp_write test/test_udp_write.cpp)
    target_link_libraries(test_udp_write ros_driver_base ${catkin_LIBRARIES})
endif()
