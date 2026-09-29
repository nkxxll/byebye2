cmake_minimum_required(VERSION 3.20)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(Byebye VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(CTest)
add_library(byebye_core STATIC src/core.c)
target_include_directories(byebye_core PUBLIC src)
if(MSVC)
  target_compile_options(byebye_core PRIVATE /W4)
else()
  target_compile_options(byebye_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(APPLE)
  # Invoke Apple's Swift compiler directly so the default Makefiles generator
  # works with Command Line Tools alone (CMake's Swift language needs Ninja/Xcode).
  find_program(SWIFTC_EXECUTABLE swiftc REQUIRED)
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK
                  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(SWIFT_SOURCES src/MacActions.swift src/MacMenu.swift)
  set(SWIFT_FLAGS -parse-as-library -swift-version 5
      -sdk "${MACOS_SDK}"
      -target "${CMAKE_SYSTEM_PROCESSOR}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}"
      -module-cache-path "${CMAKE_CURRENT_BINARY_DIR}/swift-module-cache"
      -I "${CMAKE_CURRENT_SOURCE_DIR}/src/ByebyeCore")
  set(APP_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/Byebye.app")
  file(MAKE_DIRECTORY "${APP_BUNDLE}/Contents/MacOS")
  configure_file(packaging/Info.plist "${APP_BUNDLE}/Contents/Info.plist" @ONLY)
  add_custom_command(OUTPUT "${APP_BUNDLE}/Contents/MacOS/Byebye"
      COMMAND "${SWIFTC_EXECUTABLE}" ${SWIFT_FLAGS} $<IF:$<CONFIG:Debug>,-g,-O>
              ${SWIFT_SOURCES} src/ByebyeApp.swift $<TARGET_FILE:byebye_core>
              -o "${APP_BUNDLE}/Contents/MacOS/Byebye"
      DEPENDS ${SWIFT_SOURCES} src/ByebyeApp.swift src/core.h
              src/ByebyeCore/module.modulemap byebye_core
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
  add_custom_target(byebye ALL DEPENDS "${APP_BUNDLE}/Contents/MacOS/Byebye")
  install(DIRECTORY "${APP_BUNDLE}" DESTINATION . USE_SOURCE_PERMISSIONS)
elseif(WIN32)
  add_executable(byebye WIN32 src/windows.c)
  target_compile_definitions(byebye PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
  if(MSVC)
    target_compile_options(byebye PRIVATE /W4 /utf-8)
    target_compile_options(byebye_core PRIVATE /utf-8)
  endif()
  target_link_libraries(byebye PRIVATE byebye_core user32 gdi32 advapi32 powrprof shell32)
  install(TARGETS byebye RUNTIME DESTINATION bin)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.22)
  add_executable(byebye src/linux.c)
  target_compile_options(byebye PRIVATE -Wall -Wextra)
  target_link_libraries(byebye PRIVATE byebye_core PkgConfig::GTK3)
  install(TARGETS byebye RUNTIME DESTINATION bin)
  install(FILES packaging/byebye.desktop DESTINATION share/applications)
else()
  message(FATAL_ERROR "Supported platforms: macOS, Windows, Linux")
endif()
if(BUILD_TESTING)
  add_executable(core_tests tests/core_test.c)
  target_link_libraries(core_tests PRIVATE byebye_core)
  add_test(NAME core COMMAND core_tests)
  if(APPLE)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/macos_ui_tests"
        COMMAND "${SWIFTC_EXECUTABLE}" ${SWIFT_FLAGS} ${SWIFT_SOURCES}
                tests/MacMenuTests.swift $<TARGET_FILE:byebye_core>
                -o "${CMAKE_CURRENT_BINARY_DIR}/macos_ui_tests"
        DEPENDS ${SWIFT_SOURCES} tests/MacMenuTests.swift src/core.h
                src/ByebyeCore/module.modulemap byebye_core
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
    add_custom_target(macos_ui_test_build ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/macos_ui_tests")
    # Run explicitly: requires a graphical login session; never performs OS actions.
  endif()
endif()
