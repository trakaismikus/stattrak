cmake_minimum_required(VERSION 3.16)

# BLE helper component that comes with ESP-IDF. It is added here, and not in
# main/idf_component.yml, so that dependencies.lock has no path of one computer in it.
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/bluetooth/nimble/common/nimble_peripheral_utils)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# The firmware version is the text in the file version.txt. ESP-IDF reads that file.
# It does not come from git tags: a copy of this project that was downloaded as a ZIP
# file, or a repository with no tags, has no version there.


project(stattrak)