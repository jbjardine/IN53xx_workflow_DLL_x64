cmake_minimum_required(VERSION 3.20)
project(uhf_wrapper LANGUAGES C CXX)

add_library(UhfWrapper SHARED
  uhf_wrapper.cpp
)

target_include_directories(UhfWrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
  set(VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/53 serial products SDK/SW_SDK_V4.0/Library")
  set(SWHID_LIB_X64 "${VENDOR_ROOT}/X64/USB/SWHidApi.lib")
  set(SWHID_LIB_X86 "${VENDOR_ROOT}/X86/USB/SWHidApi.lib")
  set(SWHID_DLL_X64 "${VENDOR_ROOT}/X64/USB/SWHidApi.dll")
  set(SWHID_DLL_X86 "${VENDOR_ROOT}/X86/USB/SWHidApi.dll")
  set(DEF_X64 "${CMAKE_CURRENT_SOURCE_DIR}/UhfWrapper.x64.def")
  set(DEF_X86 "${CMAKE_CURRENT_SOURCE_DIR}/UhfWrapper.x86.def")
  option(UHF_REQUIRE_VENDOR_LIB "Fail configure when vendor import lib is missing" OFF)
  option(UHF_MINIMAL_EXPORTS_ONLY "Export only UHF_* symbols (CI mode without vendor import lib)" OFF)

  target_compile_definitions(UhfWrapper PRIVATE UHF_WRAPPER_EXPORTS)
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DEF_FILE "${DEF_X64}")
    set(SWHID_LIB "${SWHID_LIB_X64}")
    set(SWHID_DLL "${SWHID_DLL_X64}")
  else()
    set(DEF_FILE "${DEF_X86}")
    set(SWHID_LIB "${SWHID_LIB_X86}")
    set(SWHID_DLL "${SWHID_DLL_X86}")
  endif()

  set(UHF_USE_MINIMAL_DEF OFF)
  if (UHF_MINIMAL_EXPORTS_ONLY)
    set(UHF_USE_MINIMAL_DEF ON)
  elseif (EXISTS "${SWHID_LIB}")
    target_link_libraries(UhfWrapper PRIVATE "${SWHID_LIB}")
  else()
    if (UHF_REQUIRE_VENDOR_LIB)
      message(FATAL_ERROR "Vendor import library not found: ${SWHID_LIB}")
    endif()
    set(UHF_USE_MINIMAL_DEF ON)
  endif()

  if (UHF_USE_MINIMAL_DEF)
    set(DEF_FILE_MINIMAL "${CMAKE_CURRENT_BINARY_DIR}/UhfWrapper.minimal.def")
    file(READ "${DEF_FILE}" _def_input)
    string(REPLACE "\r\n" "\n" _def_input "${_def_input}")
    string(REPLACE "\r" "\n" _def_input "${_def_input}")
    string(REPLACE "\n" ";" _def_lines "${_def_input}")

    set(_def_output "LIBRARY \"UhfWrapper\"\nEXPORTS\n")
    foreach(_line IN LISTS _def_lines)
      string(STRIP "${_line}" _line_trim)
      if (_line_trim STREQUAL "" OR
          _line_trim STREQUAL "LIBRARY \"UhfWrapper\"" OR
          _line_trim STREQUAL "EXPORTS")
        continue()
      endif()
      if (_line_trim MATCHES "^UHF_")
        string(APPEND _def_output "  ${_line_trim}\n")
      endif()
    endforeach()

    file(WRITE "${DEF_FILE_MINIMAL}" "${_def_output}")
    set(DEF_FILE "${DEF_FILE_MINIMAL}")
    message(STATUS "Using minimal DEF exports (vendor lib not required): ${DEF_FILE}")
  endif()

  set_target_properties(UhfWrapper PROPERTIES
    OUTPUT_NAME "UhfWrapper"
    LINK_FLAGS "/DEF:${DEF_FILE}"
  )

  if (EXISTS "${SWHID_DLL}")
    add_custom_command(TARGET UhfWrapper POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SWHID_DLL}" "$<TARGET_FILE_DIR:UhfWrapper>"
      COMMENT "Copy vendor SWHidApi.dll"
    )
  endif()
else()
  target_compile_definitions(UhfWrapper PRIVATE UHF_WRAPPER_EXPORTS)
endif()

add_executable(UhfWrapperCli uhf_cli.cpp)
target_link_libraries(UhfWrapperCli PRIVATE UhfWrapper)

option(UHF_BUILD_TESTS "Build unit tests" OFF)
if (UHF_BUILD_TESTS)
  enable_testing()
  add_executable(UhfWrapperTests tests/uhf_wrapper_tests.cpp)
  target_link_libraries(UhfWrapperTests PRIVATE UhfWrapper)
  add_test(NAME UhfWrapperTests COMMAND UhfWrapperTests)
endif()
