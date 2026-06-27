cmake_minimum_required(VERSION 3.10)
project(live2d_native)

# App name for the shared library
set(APP_NAME live2d_native)

# Directory paths
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(COMMON_SRC_PATH ${SDK_ROOT_PATH}/Samples/Samples/Common)
set(STB_PATH ${SDK_ROOT_PATH}/Samples/Samples/OpenGL/thirdParty/stb)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Import Cubism Core as static library
add_library(Live2DCubismCore STATIC IMPORTED)
set_target_properties(Live2DCubismCore PROPERTIES
    IMPORTED_LOCATION
        ${CORE_PATH}/lib/android/${ANDROID_ABI}/libLive2DCubismCore.a
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
)

# Cubism Framework renderer selection
set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
target_compile_definitions(Framework PUBLIC CSM_TARGET_ANDROID_ES2)

# App shared library
add_library(${APP_NAME} SHARED)
add_subdirectory(${COMMON_SRC_PATH} ${CMAKE_CURRENT_BINARY_DIR}/src/Common)
add_subdirectory(src/main/cpp)

target_link_libraries(${APP_NAME}
    Framework
    Common
    Live2DCubismCore
    GLESv2
    GLESv1_CM
    log
)

target_include_directories(${APP_NAME} PRIVATE ${STB_PATH})
target_link_options(${APP_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
