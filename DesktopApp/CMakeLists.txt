cmake_minimum_required(VERSION 3.21)
project(FloriDesktop LANGUAGES CXX)

# ============================================================
#  芙洛理 Flori · Windows 原生桌面端
#  D3D11 + Direct2D + DirectWrite，零第三方依赖
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus /EHsc)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

file(GLOB_RECURSE FLORI_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${FLORI_SOURCES})

add_executable(Flori WIN32 ${FLORI_SOURCES})

target_include_directories(Flori PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(Flori PRIVATE
  d3d11 dxgi d2d1 dwrite d3dcompiler windowscodecs
  dwmapi shcore user32 ole32 shell32 uuid winhttp
  ws2_32 bcrypt          # §3 实时：手写 RFC 6455 over Winsock + CNG(SHA1/随机数)
  mfplat mfreadwrite mfuuid   # #28 背景音乐：MF 解码 MP3→PCM（系统组件）
  xaudio2                # #28 背景音乐：XAudio2 播放（系统组件）
  imm32                  # P1-2 中文 IME：组合串读取 + 候选窗定位（系统组件）
  psapi)                 # P1-5 前台学习检测：GetModuleBaseNameW / QueryFullProcessImageNameW

# 开发期允许直接读源码目录里的 shader，改完按 F5 热重载
target_compile_definitions(Flori PRIVATE
  FLORI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# 资源（着色器）随构建复制到 exe 旁
add_custom_command(TARGET Flori POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Flori>/assets"
  COMMENT "复制 assets 到输出目录")

# ============================================================
#  契约回归测试 ws_contract_test（P0-3）
#  复用生产网络层 (src/net/Http.cpp + src/net/Realtime.cpp) 连运行中的 Server，
#  验证 §3 实时 / §4 专栏 / §5 媒体 的协议口径。独立可执行，不影响 Flori 主体构建。
#  退出码：0=全部联通；1=前置准备失败；2=有未联通项。
# ============================================================
add_executable(ws_contract_test
  tools/ws_contract_test.cpp
  src/net/Http.cpp
  src/net/Realtime.cpp)
target_include_directories(ws_contract_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ws_contract_test PRIVATE winhttp ws2_32 bcrypt)

  
