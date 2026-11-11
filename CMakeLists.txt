cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# Get project version from git tags
execute_process(
    COMMAND git describe --tags --dirty --always
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(GIT_DESCRIBE_VERSION)
  string(REGEX REPLACE
      "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*$"
      "\\1"
      PROJECT_VER
      "${GIT_DESCRIBE_VERSION}"
  )
  message("Project version: " ${PROJECT_VER})
endif()


project(stattrak)