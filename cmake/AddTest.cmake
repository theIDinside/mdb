set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
include(GoogleTest)

# Build explicit DWARF 4 and DWARF 5 subjects. Put shared compiler flags in
# COMPILE_OPTIONS so both targets receive them.
function(BuildTestSubject)
  set(options SHARED)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBS INCLUDE_DIR COMPILE_OPTIONS)
  cmake_parse_arguments(BUILD_TEST "${options}"
                        "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  foreach(suffix IN ITEMS "" "_d5")
    if(suffix)
      set(dwarf 5)
    else()
      set(dwarf 4)
    endif()
    set(target_name "${BUILD_TEST_NAME}${suffix}")
    if(BUILD_TEST_SHARED)
      add_library(${target_name} SHARED ${BUILD_TEST_SOURCES})
    else()
      add_executable(${target_name} ${BUILD_TEST_SOURCES})
    endif()
    set_target_properties(${target_name} PROPERTIES
      LINKER_LANGUAGE CXX
      OUTPUT_NAME "${BUILD_TEST_NAME}"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/d${dwarf}"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/d${dwarf}")
    # Subject libraries must be declared before the subjects that use them.
    foreach(lib IN LISTS BUILD_TEST_LIBS)
      if(suffix AND TARGET "${lib}${suffix}")
        target_link_libraries(${target_name} PRIVATE "${lib}${suffix}")
      else()
        target_link_libraries(${target_name} PRIVATE ${lib})
      endif()
    endforeach()
    target_include_directories(${target_name} PRIVATE ./src ${BUILD_TEST_INCLUDE_DIR})
    if(dwarf EQUAL 5)
      target_compile_options(${target_name} PRIVATE -O0 -g3 -gdwarf-5 -fdebug-types-section)
    else()
      target_compile_options(${target_name} PRIVATE -O0 -g3 -gdwarf-4)
    endif()
    target_compile_options(${target_name} PRIVATE ${BUILD_TEST_COMPILE_OPTIONS})
  endforeach()
endfunction()

function(AddTest)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBS INCLUDE_DIR)
  cmake_parse_arguments(ADD_TEST "${options}"
                        "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_executable(${ADD_TEST_NAME} ${ADD_TEST_SOURCES})
  target_link_libraries(${ADD_TEST_NAME} ${ADD_TEST_LIBS} GTest::gtest_main)
  target_include_directories(${ADD_TEST_NAME} PRIVATE ./src ${ADD_TEST_INCLUDE_DIR})
  # Test code is never built with warnings (and therefore never fails on -Werror)
  target_compile_options(${ADD_TEST_NAME} PRIVATE -w)
  gtest_discover_tests(${ADD_TEST_NAME})
endfunction()
