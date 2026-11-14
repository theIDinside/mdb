cmake_minimum_required(VERSION 3.22)
project(mdb_tests CXX C ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Test subjects are never built with warnings (and therefore never fail on -Werror)
add_compile_options(-w)

message(MDB ROOT IS: ${MDB_ROOT})
include(${MDB_ROOT}/cmake/AddTest.cmake)

# set(CMAKE_CXX_FLAGS_DEBUG "${COMMON_FLAGS} -g3 -fomit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "${COMMON_FLAGS} -g3")

set(MDB_TESTS
  helloworld
  pause
  threads_shared # all tests that uses shared libraries, must have shared in their name
  stackframes
  next
  lib_shared # all tests that uses shared libraries, must have shared in their name
  stupid_shared
  basetypes # testing of variable symbol parsing for base types and POD types
  variables
  logpoints
)

# For tests with additional sources (beyond the sources with the same name of the test, that is)
# name their variables by their names with _SOURCES appended
set(stupid_shared_SOURCES todo.cpp)

BuildTestSubject(NAME metricsconv SHARED SOURCES dynamic_lib.cpp dynamic_lib.h)

foreach(name IN LISTS MDB_TESTS)
  set(libs "")
  if(name MATCHES "shared")
    list(APPEND libs metricsconv)
  endif()
  BuildTestSubject(
    NAME ${name}
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/spinlock.cpp ${${name}_SOURCES}
    LIBS ${libs}
  )
endforeach()

BuildTestSubject(NAME logpoints_opt SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/logpoints.cpp COMPILE_OPTIONS -O2)

# view() calls and dynamic types. Gecko is built without RTTI; dynamic types must not need it.
BuildTestSubject(NAME dynamic_types SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_types.cpp COMPILE_OPTIONS -fno-rtti)

BuildTestSubject(
  NAME forever
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/forever.cpp
)

BuildTestSubject(
  NAME signals
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/signals.cpp
)

BuildTestSubject(
  NAME templated
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/templated_code/template_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/templated_code/template_foo.cpp
)

BuildTestSubject(
  NAME readMemory
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/readMemory.cpp
)

BuildTestSubject(
  NAME pmr
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/pmr.cpp
)

BuildTestSubject(
  NAME functionBreakpoints
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/functionBreakpoints.cpp
)

BuildTestSubject(
  NAME dynamicLoading
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/dynamicLoading.cpp
  LIBS dl
)
add_dependencies(dynamicLoading metricsconv)
add_dependencies(dynamicLoading_d5 metricsconv_d5)
target_compile_definitions(dynamicLoading PRIVATE METRICSCONV_LIBRARY="$<TARGET_FILE:metricsconv>")
target_compile_definitions(dynamicLoading_d5 PRIVATE METRICSCONV_LIBRARY="$<TARGET_FILE:metricsconv_d5>")

BuildTestSubject(
  NAME fork
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/fork.cpp
)

BuildTestSubject(
  NAME mandelbrot
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/mandelbrot.cpp
)

BuildTestSubject(
  NAME avx-subject
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/avx-subject.cpp
)

BuildTestSubject(
  NAME lnp-test
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/lnp/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/lnp/a.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/lnp/b.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/lnp/a.h
  ${CMAKE_CURRENT_SOURCE_DIR}/lnp/b.h
  INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lnp
)

set(VariablesRequestSubjects arrayOf3 pointer struct)
set(MultiProcessSubjects parent childprogram)

foreach(name ${VariablesRequestSubjects})
  BuildTestSubject(
    NAME ${name}
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/subjects/variablesRequest/${name}.cpp
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
endforeach()

foreach(name ${MultiProcessSubjects})
  BuildTestSubject(
    NAME ${name}
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/subjects/multiprocess/${name}.cpp
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
endforeach()

BuildTestSubject(NAME miniipc SHARED
  SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/subjects/minibrowser/ipc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/subjects/minibrowser/ipc.h)

set(MiniBrowserSubjects browser content)

foreach(name ${MiniBrowserSubjects})
  BuildTestSubject(
    NAME ${name}
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/subjects/minibrowser/${name}.cpp
    LIBS miniipc
    INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/subjects/minibrowser
  )
endforeach()

BuildTestSubject(
  NAME strings
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/strings.cpp
)

BuildTestSubject(
  NAME containers
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/containers.cpp
)

BuildTestSubject(
  NAME stl
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stl.cpp
)

# The same program, but the compiler leaves template arguments out of type names; views must rebuild them from the
# template parameters in the debug info.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  BuildTestSubject(
    NAME containers_simple_template_names
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/containers.cpp
    COMPILE_OPTIONS -gsimple-template-names
  )
endif()

message("Test build type: ${CMAKE_BUILD_TYPE}")
