cmake_minimum_required(VERSION 3.9)
project(minapp VERSION 1.0.0)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS None Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost 1.66 REQUIRED COMPONENTS system)

set(SOURCE_FILES
        src/handler.cpp
        src/session.cpp
        src/service.cpp
        src/attribute_set.cpp
)
add_library(${PROJECT_NAME} ${SOURCE_FILES})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
)

# https://cmake.org/cmake/help/latest/prop_tgt/INTERFACE_INCLUDE_DIRECTORIES.html
# https://stackoverflow.com/questions/25676277/cmake-target-include-directories-prints-an-error-when-i-try-to-add-the-source
target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::boost Boost::system)
target_compile_definitions(${PROJECT_NAME} PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

get_target_property(PROJECT_LIBRARY_TYPE ${PROJECT_NAME} TYPE)
string(TOUPPER ${PROJECT_NAME} PROJECT_UPPER_NAME)
string(MAKE_C_IDENTIFIER ${PROJECT_UPPER_NAME} PROJECT_UPPER_NAME)
target_compile_definitions(${PROJECT_NAME} INTERFACE ${PROJECT_UPPER_NAME}_${PROJECT_LIBRARY_TYPE})


include(GNUInstallDirs)
set(ConfigPackageLocation ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
#set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/${PROJECT_NAME} include/boost DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${PROJECT_NAME}-targets DESTINATION ${ConfigPackageLocation} NAMESPACE ${PROJECT_NAME}::)

export(EXPORT ${PROJECT_NAME}-targets NAMESPACE ${PROJECT_NAME}::)


include(CMakePackageConfigHelpers)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in"
"@PACKAGE_INIT@
include(\"\${CMAKE_CURRENT_LIST_DIR}/${PROJECT_NAME}-targets.cmake\")
if(NOT TARGET Boost::system)
    include(CMakeFindDependencyMacro)
    find_dependency(Boost COMPONENTS system)
endif()
check_required_components(${PROJECT_NAME})
")

configure_package_config_file("${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        INSTALL_DESTINATION ${ConfigPackageLocation})

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
        COMPATIBILITY SameMinorVersion)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
        DESTINATION ${ConfigPackageLocation})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
