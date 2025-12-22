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

  target_compile_definitions(UhfWrapper PRIVATE UHF_WRAPPER_EXPORTS)
  set_target_properties(UhfWrapper PROPERTIES
    OUTPUT_NAME "UhfWrapper"
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/UhfWrapper.def"
  )

  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_link_libraries(UhfWrapper PRIVATE "${SWHID_LIB_X64}")
    set(SWHID_DLL "${SWHID_DLL_X64}")
  else()
    target_link_libraries(UhfWrapper PRIVATE "${SWHID_LIB_X86}")
    set(SWHID_DLL "${SWHID_DLL_X86}")
  endif()

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
