cmake_minimum_required(VERSION 3.22)
project(dolbyrouter_zygisk CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# shadowhook (static, arm64 only) - bundled copied source
# ------------------------------------------------------------------
set(SH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/shadowhook)
set(SH_ARCH arm64)

file(GLOB SH_SRC
    ${SH_DIR}/*.c
    ${SH_DIR}/arch/${SH_ARCH}/*.c
    ${SH_DIR}/arch/${SH_ARCH}/*.S
    ${SH_DIR}/common/*.c
    ${SH_DIR}/third_party/*/*.c)

add_library(shadowhook STATIC ${SH_SRC})
set_target_properties(shadowhook PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(shadowhook PRIVATE c_std_11)
# Upstream builds with -Weverything -Werror; relax for our embedded build.
target_compile_options(shadowhook PRIVATE
    -Os -ffunction-sections -fdata-sections -fvisibility=hidden
    -Wno-everything -Wno-reserved-identifier)
target_include_directories(shadowhook PRIVATE
    ${SH_DIR}
    ${SH_DIR}/include
    ${SH_DIR}/arch/${SH_ARCH}
    ${SH_DIR}/common
    ${SH_DIR}/third_party/xdl
    ${SH_DIR}/third_party/bsd
    ${SH_DIR}/third_party/lss)
target_link_libraries(shadowhook PRIVATE log)

# ------------------------------------------------------------------
# Zygisk module -> zygisk/arm64-v8a.so  (no "lib" prefix)
# ------------------------------------------------------------------
add_library(dolbyrouter SHARED src/dolby_router.cpp)
set_target_properties(dolbyrouter PROPERTIES PREFIX "" OUTPUT_NAME "arm64-v8a")
target_include_directories(dolbyrouter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SH_DIR}/include)
target_compile_options(dolbyrouter PRIVATE
    -Oz -fvisibility=hidden -ffunction-sections -fdata-sections -Wall)
target_link_options(dolbyrouter PRIVATE
    -Wl,--gc-sections -Wl,-z,max-page-size=16384)
target_link_libraries(dolbyrouter PRIVATE shadowhook log)
