cmake_minimum_required(VERSION 3.14)
project(irods_resource_plugin_conveyor)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# --- FIX: Tell CMake where to look for FindIRODS.cmake ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Dependencies ---
find_package(IRODS REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

include(FetchContent)
FetchContent_Declare(
  libconveyor
  GIT_REPOSITORY https://github.com/jasoncoposky/libconveyor.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(libconveyor)

# --- Build Plugin ---
add_library(irods_resource_plugin_conveyor MODULE src/libconveyor_resource.cpp)

target_compile_definitions(irods_resource_plugin_conveyor PRIVATE RODS_SERVER IRODS_ENABLE_SYSLOG)

target_include_directories(irods_resource_plugin_conveyor PRIVATE
    ${IRODS_INCLUDE_DIRS}
    /opt/irods-externals/boost1.81.0-2/include
    ${libconveyor_SOURCE_DIR}/include
)

target_link_libraries(irods_resource_plugin_conveyor PRIVATE
    IRODS::IRODS
    fmt::fmt
    spdlog::spdlog
    conveyor
)

# --- Install ---
set(CMAKE_INSTALL_PREFIX "/")

install(TARGETS irods_resource_plugin_conveyor
    LIBRARY DESTINATION /usr/lib/irods/plugins/resources
)

set(IRODS_SCRIPTS_DIRECTORY "/var/lib/irods/scripts/irods/test")

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/test_irods_resource_plugin_conveyor.py"
    DESTINATION "${IRODS_SCRIPTS_DIRECTORY}"
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)

# --- Packaging ---
set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "irods-resource-plugin-conveyor")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_MAINTAINER "iRODS Development Team <info@irods.org>")
set(CPACK_PACKAGE_CONTACT "info@irods.org")
set(CPACK_PACKAGE_DESCRIPTION "iRODS Conveyor Resource Plugin")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}-${CMAKE_SYSTEM_PROCESSOR}")

# Debian
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "irods-server (>= 4.3.0)")

# RPM
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst")
set(CPACK_RPM_PACKAGE_REQUIRES "irods-server >= 4.3.0")

include(CPack)

# --- Testing ---
add_custom_target(run_tests
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=/var/lib/irods/scripts python3 -m unittest irods.test.test_irods_resource_plugin_conveyor
    WORKING_DIRECTORY /var/lib/irods/scripts
    COMMENT "Running iRODS conveyor plugin tests"
)
