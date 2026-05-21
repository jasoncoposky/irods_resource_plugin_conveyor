# cmake/FindIRODS.cmake
include(FindPackageHandleStandardArgs)

# 1. Search for Headers
find_path(IRODS_INCLUDE_DIR NAMES irods/rodsDef.h
    HINTS
        /usr/include/irods
        /usr/local/include/irods
        $ENV{IRODS_HOME}/include
    PATH_SUFFIXES irods
)

# 2. Search for Libraries (Client and Common)
find_library(IRODS_COMMON_LIB NAMES irods_common
    HINTS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        $ENV{IRODS_HOME}/lib
)

find_library(IRODS_CLIENT_LIB NAMES irods_client
    HINTS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        $ENV{IRODS_HOME}/lib
)

# 3. Handle Version (Optional but good)
if(IRODS_INCLUDE_DIR AND EXISTS "${IRODS_INCLUDE_DIR}/irods/rodsDef.h")
    file(STRINGS "${IRODS_INCLUDE_DIR}/irods/rodsDef.h" IRODS_VERSION_MAJOR_LINE REGEX "^#define [ \t]+RODS_MAJOR_VERSION[ \t]+[0-9]+$")
    file(STRINGS "${IRODS_INCLUDE_DIR}/irods/rodsDef.h" IRODS_VERSION_MINOR_LINE REGEX "^#define [ \t]+RODS_MINOR_VERSION[ \t]+[0-9]+$")
    file(STRINGS "${IRODS_INCLUDE_DIR}/irods/rodsDef.h" IRODS_VERSION_PATCH_LINE REGEX "^#define [ \t]+RODS_PATCH_VERSION[ \t]+[0-9]+$")

    string(REGEX REPLACE "^#define [ \t]+RODS_MAJOR_VERSION[ \t]+([0-9]+)$" "\\1" IRODS_VERSION_MAJOR "${IRODS_VERSION_MAJOR_LINE}")
    string(REGEX REPLACE "^#define [ \t]+RODS_MINOR_VERSION[ \t]+([0-9]+)$" "\\1" IRODS_VERSION_MINOR "${IRODS_VERSION_MINOR_LINE}")
    string(REGEX REPLACE "^#define [ \t]+RODS_PATCH_VERSION[ \t]+([0-9]+)$" "\\1" IRODS_VERSION_PATCH "${IRODS_VERSION_PATCH_LINE}")

    set(IRODS_VERSION "${IRODS_VERSION_MAJOR}.${IRODS_VERSION_MINOR}.${IRODS_VERSION_PATCH}")
endif()

# 4. Standard Args Handling
find_package_handle_standard_args(IRODS
    REQUIRED_VARS IRODS_INCLUDE_DIR IRODS_COMMON_LIB IRODS_CLIENT_LIB
    VERSION_VAR IRODS_VERSION
)

# 5. Create Imported Target (Modern CMake)
if(IRODS_FOUND AND NOT TARGET IRODS::IRODS)
    add_library(IRODS::IRODS UNKNOWN IMPORTED)
    set_target_properties(IRODS::IRODS PROPERTIES
        IMPORTED_LOCATION "${IRODS_COMMON_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${IRODS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${IRODS_CLIENT_LIB}"
    )
    # On some systems, you need to link against rt, dl, etc.
    if(UNIX)
        set_property(TARGET IRODS::IRODS APPEND PROPERTY INTERFACE_LINK_LIBRARIES dl pthread)
    endif()
endif()

# Export variables for legacy CMake
set(IRODS_LIBRARIES ${IRODS_COMMON_LIB} ${IRODS_CLIENT_LIB})
set(IRODS_INCLUDE_DIRS ${IRODS_INCLUDE_DIR})
