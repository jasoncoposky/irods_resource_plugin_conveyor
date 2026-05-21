# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-src"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-build"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/tmp"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/src/libconveyor-populate-stamp"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/src"
  "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/src/libconveyor-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/src/libconveyor-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/darkfell/dev/irods_resource_plugin_conveyor/build_standalone/_deps/libconveyor-subbuild/libconveyor-populate-prefix/src/libconveyor-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
