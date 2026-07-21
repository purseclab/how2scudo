cmake_minimum_required(VERSION 3.22.1)

project(how2scudo_33ad5959 LANGUAGES C)

if(NOT ANDROID)
  message(FATAL_ERROR "Configure this target with the Android NDK toolchain")
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "This build-ID-specific PoC requires arm64-v8a")
endif()

add_executable(forged_commit_base forged_commit_base.c)
target_compile_features(forged_commit_base PRIVATE c_std_11)
target_compile_definitions(forged_commit_base PRIVATE HOW2SCUDO_STANDALONE=1)
target_compile_options(forged_commit_base PRIVATE
  -O0
  -g3
  -fno-omit-frame-pointer
  -Wall
  -Wextra
)
target_link_libraries(forged_commit_base PRIVATE dl log)

add_executable(house_of_spirit house_of_spirit.c)
target_compile_features(house_of_spirit PRIVATE c_std_11)
target_compile_definitions(house_of_spirit PRIVATE HOW2SCUDO_STANDALONE=1)
target_compile_options(house_of_spirit PRIVATE
  -O0
  -g3
  -fno-omit-frame-pointer
  -Wall
  -Wextra
)
target_link_libraries(house_of_spirit PRIVATE dl log)
