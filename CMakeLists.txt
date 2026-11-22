cmake_minimum_required(VERSION 3.18)

project(YiufPlayer
    VERSION 1.0.0
    DESCRIPTION "Controller & Remote Native Media Player for Handhelds, TV & Desktop"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
endif ()

add_compile_options(-Wall -Wextra)

# macOS / Homebrew 路径补充（架构交由 CMake 与工具链决定，不再硬编码 arm64）
if (APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif ()

# 寻找外部依赖
find_package(PkgConfig REQUIRED)

# MPV_ROOT：指向自建的 LGPL 版 libmpv 安装前缀。
# 不指定时使用系统/Homebrew 的 mpv（通常为 GPL 构建，仅可用于本地自测）。
if (DEFINED MPV_ROOT AND NOT MPV_ROOT STREQUAL "")
    message(STATUS "使用自建 libmpv: ${MPV_ROOT}")
    set(ENV{PKG_CONFIG_PATH} "${MPV_ROOT}/lib/pkgconfig:${MPV_ROOT}/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    list(APPEND CMAKE_PREFIX_PATH "${MPV_ROOT}")
endif ()

if (ANDROID)
    # Android 没有 pkg-config，libmpv 由 NDK 侧的预编译 .so 提供。
    # 头文件用仓库内自带的（版本与预编译库匹配），库从 jniLibs 链接。
    # ANDROID_MPV_HEADERS / ANDROID_MPV_LIBS 可在 app/build.gradle 里覆盖。
    if (NOT DEFINED ANDROID_MPV_HEADERS)
        set(ANDROID_MPV_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/external/mpv-headers")
    endif ()
    if (NOT DEFINED ANDROID_MPV_LIBS)
        set(ANDROID_MPV_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/jniLibs/${ANDROID_ABI}")
    endif ()

    if (NOT EXISTS "${ANDROID_MPV_HEADERS}/mpv/client.h")
        message(FATAL_ERROR
            "Android 构建缺少 mpv 头文件。\n"
            "请把 mpv 头文件放到 ${ANDROID_MPV_HEADERS}（需要 mpv/client.h 与 render.h），\n"
            "或用 -DANDROID_MPV_HEADERS=<路径> 指定。")
    endif ()

    set(MPV_INCLUDE_DIRS "${ANDROID_MPV_HEADERS}")
    set(MPV_LIBRARY_DIRS "${ANDROID_MPV_LIBS}")
    set(MPV_LIBRARIES mpv)
    message(STATUS "libmpv（Android 预编译）：${ANDROID_MPV_LIBS}")
else ()
    pkg_check_modules(MPV REQUIRED mpv)
endif ()

# 若指定了 MPV_ROOT，确认 pkg-config 解析到的确实来自该前缀，
# 否则会在用户不知情的情况下回退到系统的 GPL 版 mpv。
if (DEFINED MPV_ROOT AND NOT MPV_ROOT STREQUAL "")
    set(_mpv_found_path "")
    foreach(_d ${MPV_LIBRARY_DIRS})
        if (_d MATCHES "^${MPV_ROOT}")
            set(_mpv_found_path "${_d}")
            break()
        endif ()
    endforeach()
    if (_mpv_found_path STREQUAL "")
        message(WARNING
            "指定的 MPV_ROOT (${MPV_ROOT}) 中未找到 libmpv，"
            "已回退到系统 mpv：${MPV_LIBRARY_DIRS}。"
            "这通常是 GPL 构建，若用于分发将违反许可证。")
    else ()
        message(STATUS "libmpv 来自 MPV_ROOT: ${_mpv_found_path}")
    endif ()
endif ()

# ---------------------------------------------------------------------------
# 许可证信息采集（不是防线）
#
# 本项目自 v0.7 起以 GPL-3.0 分发，链接 GPL 版 libmpv / FFmpeg 完全合法。
# 此前的 ALLOW_GPL_LINKAGE 开关与「检测到 GPL 即中止」的防线已移除。
#
# 保留探针的原因：GPL-3.0 §6 要求分发时提供对应源码，
# 因此必须能查出产物实际链接的是哪个 FFmpeg 构建。
# ---------------------------------------------------------------------------
if (ANDROID)
    # Android 上跳过 license_probe：
    #   1) APK 里没有独立的可执行进程，探针跑不起来
    #   2) FFmpeg 版本信息可由发版流程人工核对
    # 桌面平台的探针仍然保留（见下）。
    message(STATUS "Android：跳过构建期许可证探针")
else ()
    pkg_check_modules(AVUTIL REQUIRED libavutil)

    add_executable(license_probe "${CMAKE_CURRENT_SOURCE_DIR}/cmake/license_probe.cpp")
target_include_directories(license_probe PRIVATE ${AVUTIL_INCLUDE_DIRS})
target_link_directories(license_probe PRIVATE ${AVUTIL_LIBRARY_DIRS})
target_link_libraries(license_probe PRIVATE ${AVUTIL_LIBRARIES})

add_custom_target(license_check ALL
    COMMAND license_probe
    COMMENT "记录 FFmpeg/libmpv 许可证信息"
    VERBATIM
)
    add_dependencies(license_check license_probe)
endif ()

# ---------------------------------------------------------------------------
# FFmpeg（可选）：列表缩略图与网格卡需要从视频里抽一帧。
#
# 刻意做成可选而不是 REQUIRED：
#   · Android 的 jniLibs 里目前只有 libmpv.so，没有单独的 avformat/avcodec；
#   · 抽帧只是一层装饰，拿不到就退回占位画面，界面照样能用。
# 所以这里 find 不到就不定义 MUVIK_HAVE_FFMPEG，thumbnail_cache 走空实现。
#
# **Android 必须整个跳过**：pkg_check_modules 没有平台意识，交叉编译时它会
# 去搜宿主机（macOS 的 Homebrew / CI 的 ubuntu）的 pkg-config 路径。
# 实测：在 macOS 上给 arm64 Android 配置时，build.ninja 的 INCLUDES 里出现了
#   -isystem /opt/homebrew/Cellar/ffmpeg/9.0.2_1/include
# 以及 -lavformat/-lavcodec/-lavutil/-lswscale（x86_64 Mach-O）与
# -DMUVIK_HAVE_FFMPEG=1 —— 宿主的库被链进了目标平台产物。
# 这会在链接期报架构不匹配，或运行期 dlopen 失败，而且报错点随宿主环境漂移。
# 对 libcurl 早就做了同样的排除（见下面 if (ANDROID) 分支），FFmpeg 漏了。
# 抽帧可以显式关掉：-DMUVIK_ENABLE_THUMBNAILS=OFF
#
# 什么时候需要关：**给 SteamOS/Arch 构建发布包时**。
# 直接链接 libavformat/libavcodec 会把具体的 soname 写进产物
# （Debian 12 上是 libavcodec.so.59 / libavutil.so.57），
# 而 SteamOS 是 Arch，FFmpeg 版本更高、soname 不同，
# 拷过去就是「error while loading shared libraries」。
#
# 关掉之后产物只依赖 libmpv（SteamOS 自带，且 soname 稳定），
# 代价是媒体库网格少了视频缩略图、退回占位画面 —— 界面照样能用。
# 这是把「跑不起来」换成「少个装饰」的取舍，发布包里通常值得。
option(MUVIK_ENABLE_THUMBNAILS "从视频抽帧生成缩略图（需要链接 FFmpeg）" ON)

if (NOT ANDROID AND MUVIK_ENABLE_THUMBNAILS)
    pkg_check_modules(AVFORMAT libavformat)
    pkg_check_modules(AVCODEC libavcodec)
    pkg_check_modules(AVUTIL libavutil)
    pkg_check_modules(SWSCALE libswscale)
endif ()
if (NOT ANDROID AND MUVIK_ENABLE_THUMBNAILS
        AND AVFORMAT_FOUND AND AVCODEC_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
    set(MUVIK_FFMPEG_FOUND TRUE)
    message(STATUS "FFmpeg 抽帧：启用（avformat ${AVFORMAT_VERSION} / avcodec ${AVCODEC_VERSION}）")
else ()
    set(MUVIK_FFMPEG_FOUND FALSE)
    if (ANDROID)
        message(STATUS "FFmpeg 抽帧：Android 跳过（不使用宿主 FFmpeg，缩略图走占位画面）")
    elseif (NOT MUVIK_ENABLE_THUMBNAILS)
        message(STATUS "FFmpeg 抽帧：已按 MUVIK_ENABLE_THUMBNAILS=OFF 关闭")
        message(STATUS "  → 产物不链接 libav*，只依赖 libmpv（跨发行版/SteamOS 更稳）")
        message(STATUS "  → 代价：媒体库网格没有视频缩略图，显示占位画面")
    else ()
        message(STATUS "FFmpeg 抽帧：未找到 avformat/avcodec/avutil/swscale，缩略图退回占位画面")
    endif ()
endif ()

# libcurl：为插件提供 HTTP 能力。许可证为 MIT/X 风格，允许闭源商业分发，
# 仅需在第三方声明中保留版权信息（见 THIRD-PARTY-NOTICES.md）。
if (ANDROID)
    # Android 上没有系统 libcurl 的 pkg-config。
    # 首版目标只是「能在设备上看到界面并播放本地视频」，
    # 因此网络源插件在 Android 上暂不启用；
    # 后续可把 curl 交叉编译进 jniLibs 再打开。
    message(STATUS "Android：暂不链接 libcurl（网络源插件不可用）")
    set(CURL_FOUND FALSE)
else ()
    find_package(CURL REQUIRED)
endif ()

if (APPLE)
    find_package(OpenGL REQUIRED)
    set(PLATFORM_LIBS
        OpenGL::GL
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreWLAN"
        "-framework ForceFeedback"
        "-framework Foundation"
    )
elseif (ANDROID)
    # Android 没有桌面 OpenGL，也没有 pkg-config 里的 gl.pc。
    # 图形接口由 NDK 提供的 GLESv3 + EGL 承担，SDL2 负责窗口与输入。
    # 因此这里不能走 pkg_check_modules(GL REQUIRED gl)，
    # 否则配置阶段就会失败。
    set(PLATFORM_LIBS
        GLESv3
        EGL
        log
        android
    )
    message(STATUS "平台：Android（GLESv3 + EGL）")
else ()
    if (MUVIK_USE_GLES2 OR MUVIK_USE_GLES3)
        pkg_check_modules(GLES REQUIRED glesv2)
        if (MUVIK_USE_EGL)
            pkg_check_modules(EGL REQUIRED egl)
            set(PLATFORM_LIBS ${GLES_LIBRARIES} ${EGL_LIBRARIES})
        else ()
            set(PLATFORM_LIBS ${GLES_LIBRARIES})
        endif ()
        message(STATUS "平台：Linux GLES (${PLATFORM_LIBS})")
    else ()
        pkg_check_modules(GL REQUIRED gl)
        set(PLATFORM_LIBS ${GL_LIBRARIES})
    endif ()
endif ()

# Borealis 路径设置（优先使用项目内置的 external/borealis，保证构建可复现）
if (NOT DEFINED BOREALIS_DIR)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/CMakeLists.txt")
        set(BOREALIS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis")
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/library/borealis")
        set(BOREALIS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/library/borealis")
    endif ()
endif ()

if (NOT EXISTS "${BOREALIS_DIR}/library/CMakeLists.txt")
    message(FATAL_ERROR
        "Borealis 未找到。请确认 external/borealis 存在，"
        "或通过 -DBOREALIS_DIR=<path> 指定其位置。")
endif ()

message(STATUS "Found Borealis at: ${BOREALIS_DIR}")
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)
if (NOT DEFINED PLATFORM_DESKTOP AND NOT DEFINED PLATFORM_SWITCH)
    set(PLATFORM_DESKTOP ON CACHE BOOL "Build for Desktop" FORCE)
endif ()

# ---------------------------------------------------------------------------
# 图形后端选择（移植到掌机时需要）
#
# 默认走桌面 OpenGL：Steam Deck 的 SteamOS、macOS、普通 Linux 桌面都支持。
# 开源掌机（Allwinner A133P / RK3566 等）通常只有 OpenGL ES，
# 且系统是 Buildroot、没有 X11/Wayland，需要改用 EGL 直连。
#
#   -DDECK_USE_GLES2=ON   仅 OpenGL ES 2.0（最保守，PowerVR GE8300 这类）
#   -DDECK_USE_GLES3=ON   OpenGL ES 3.0
#   -DDECK_USE_EGL=ON     用 EGL 而不是原生 GL（配合上面两项）
#
# 这三个选项会转交给 borealis 的 commonOption.cmake。
# 注意：GLES 与 EGL 的组合尚未在真机验证过，见 RELEASE-BLOCKERS.md。
# ---------------------------------------------------------------------------
option(MUVIK_USE_GLES2 "使用 OpenGL ES 2.0 渲染（掌机）" OFF)
option(MUVIK_USE_GLES3 "使用 OpenGL ES 3.0 渲染（掌机）" OFF)
option(MUVIK_USE_EGL   "使用 EGL 而非原生 GL" OFF)

# 旧名兼容：DECK_USE_GLES2 / DECK_USE_GLES3 / DECK_USE_EGL。
#
# 这三个选项在提交 3004939 里被改名为 MUVIK_USE_*，但
# android/app/build.gradle 一直还在传 -DDECK_USE_GLES3=ON -DDECK_USE_EGL=ON。
# CMake 对未定义的 -D 变量不会报错，只会静静地写进 cache 当作未使用变量，
# 于是 Android 的图形后端选择**从未生效**：
# 实测全新配置后 cache 里 USE_GLES3:BOOL=OFF、USE_EGL:BOOL=OFF，
# 而 DECK_USE_GLES3:UNINITIALIZED=ON —— 是个死变量。
# 这里把旧名接上，两套写法都能生效，不用去改各平台的构建脚本。
if (DEFINED DECK_USE_GLES2 AND DECK_USE_GLES2)
    set(MUVIK_USE_GLES2 ON)
endif ()
if (DEFINED DECK_USE_GLES3 AND DECK_USE_GLES3)
    set(MUVIK_USE_GLES3 ON)
endif ()
if (DEFINED DECK_USE_EGL AND DECK_USE_EGL)
    set(MUVIK_USE_EGL ON)
endif ()

if (MUVIK_USE_GLES2 AND MUVIK_USE_GLES3)
    message(FATAL_ERROR "MUVIK_USE_GLES2 与 MUVIK_USE_GLES3 不能同时开启。")
endif ()
if (MUVIK_USE_GLES2)
    set(USE_GLES2 ON CACHE BOOL "using OpenGL ES 2.0" FORCE)
    message(STATUS "图形后端：OpenGL ES 2.0")
elseif (MUVIK_USE_GLES3)
    set(USE_GLES3 ON CACHE BOOL "using OpenGL ES 3.0" FORCE)
    message(STATUS "图形后端：OpenGL ES 3.0")
else ()
    message(STATUS "图形后端：桌面 OpenGL")
endif ()
if (MUVIK_USE_EGL)
    set(USE_EGL ON CACHE BOOL "Using EGL instead of native GL." FORCE)
    message(STATUS "EGL：启用")
endif ()
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

# libromfs 参数必须在 add_subdirectory(borealis) 之前设置：
# borealis 的 CMakeLists 在 include(toolchain) 时就会读取它们，
# 并据此决定是否把 resources/ 编译进二进制。
# Android/iOS 上没有稳定的相对路径可用，资源必须内置，
# 所以这里无条件指定（桌面平台也可用，只是当前没启用）。
# 注意：这个名字会被拼进生成的 C++ 变量名（RomFs_<name>），
# 因此**只能用合法标识符**（字母/数字/下划线）。用连字符会直接编译失败。
set(LIBROMFS_PROJECT_NAME "muvik" CACHE STRING "" FORCE)
set(LIBROMFS_RESOURCE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/resources/romfs" CACHE STRING "" FORCE)

# 必须用 CACHE 强制覆盖：commonOption.cmake 把 USE_LIBROMFS 声明成了
# `option(... OFF)`，也就是一个 CACHE 变量。toolchain.cmake 里那句
# `set(USE_LIBROMFS ON)` 只是设置普通变量，**无法覆盖已存在的 CACHE 项**，
# 结果 recursos 一直没被编译进二进制（表现为运行时找不到任何资源）。
if (ANDROID)
    set(USE_LIBROMFS ON CACHE BOOL "" FORCE)
endif ()

include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)
add_subdirectory(${BOREALIS_LIBRARY} borealis EXCLUDE_FROM_ALL)
add_subdirectory(external/quickjs quickjs EXCLUDE_FROM_ALL)

# 收集源码
#
# CONFIGURE_DEPENDS 是必须的：GLOB 的结果在配置期就被固化成文件列表，
# 之后新增 / 删除 .cpp 都不会被感知。实际踩过：删掉 folder_picker_activity.cpp
# 之后增量构建仍在编译这个已不存在的文件，报
#   clang++: error: no such file or directory: '.../folder_picker_activity.cpp'
# 加上之后每次构建会先检查目录，变了自动重新配置。
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# macOS 上通过 Objective-C++ 调用 NSProcessInfo 抑制 App Nap，需单独收集 .mm
if (APPLE)
    file(GLOB_RECURSE SRC_OBJC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
    )
    list(APPEND SRC_FILES ${SRC_OBJC_FILES})
else ()
    # 非 macOS 平台不编译 .mm，但 BackgroundPlayback 的三个静态方法
    # 仍被 audio_player.cpp 调用。若没有任何人定义它们，
    # Linux 链接会报 undefined symbol。
    # 这里显式补一个空实现的源文件（内容随平台条件编译，安全）。
    list(APPEND SRC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/playback/background_playback_stub.cpp"
    )
endif ()

# Android 上必须产出共享库而不是可执行文件：
# SDL 的 Java 层通过 System.loadLibrary("main") 加载，
# 加载的必须是 .so，且入口符号为 SDL_main。
if (ANDROID)
    add_library(yiufplayer SHARED ${SRC_FILES})
    set_target_properties(yiufplayer PROPERTIES OUTPUT_NAME "main")
else ()
    add_executable(yiufplayer ${SRC_FILES})
    set_target_properties(yiufplayer PROPERTIES OUTPUT_NAME "yiufplayer")
endif ()

# USE_LIBROMFS 宏必须在我们自己的 target 上也定义一次。
if (USE_LIBROMFS)
    target_compile_definitions(yiufplayer PRIVATE USE_LIBROMFS)
    target_compile_definitions(borealis PRIVATE USE_LIBROMFS)
endif ()

target_include_directories(yiufplayer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    ${MPV_INCLUDE_DIRS}
)

# 把第三方（borealis 及其依赖）的头文件标记为 SYSTEM。
target_include_directories(yiufplayer SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/include/borealis/extern"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/lib/extern/fmt/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/lib/extern/nanovg"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/lib/extern/yoga/yoga"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/borealis/library/lib/extern/tweeny/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/quickjs"
)

if (NOT APPLE)
    target_include_directories(yiufplayer PRIVATE ${GL_INCLUDE_DIRS})
endif ()

if (APPLE)
    target_link_directories(yiufplayer PRIVATE
        ${MPV_LIBRARY_DIRS}
        "/opt/homebrew/lib"
        "/usr/local/lib"
    )
else ()
    target_link_directories(yiufplayer PRIVATE
        ${MPV_LIBRARY_DIRS}
    )
endif ()

target_link_libraries(yiufplayer PRIVATE
    borealis
    quickjs
    $<$<BOOL:${USE_LIBROMFS}>:${LIBROMFS_LIBRARY}>
    $<$<NOT:$<PLATFORM_ID:Android>>:CURL::libcurl>
    ${MPV_LIBRARIES}
    ${PLATFORM_LIBS}
    $<$<NOT:$<PLATFORM_ID:Android>>:pthread>
    $<$<NOT:$<PLATFORM_ID:Android>>:dl>
)

if (USE_SDL2 AND NOT ANDROID)
    find_package(SDL2 REQUIRED)
    target_include_directories(yiufplayer PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(yiufplayer PRIVATE ${SDL2_LIBRARIES})
endif ()

# 抽帧（可选）：找到 FFmpeg 才链接，否则 thumbnail_cache 里整段实现被编译掉
if (MUVIK_FFMPEG_FOUND)
    target_compile_definitions(yiufplayer PRIVATE MUVIK_HAVE_FFMPEG=1)
    target_include_directories(yiufplayer SYSTEM PRIVATE
        ${AVFORMAT_INCLUDE_DIRS} ${AVCODEC_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS})
    target_link_directories(yiufplayer PRIVATE
        ${AVFORMAT_LIBRARY_DIRS} ${AVCODEC_LIBRARY_DIRS} ${AVUTIL_LIBRARY_DIRS} ${SWSCALE_LIBRARY_DIRS})
    target_link_libraries(yiufplayer PRIVATE
        ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES})
endif ()

# 针对 macOS 的 rpath 优化
if (APPLE)
    set_target_properties(yiufplayer PROPERTIES
        MACOSX_RPATH TRUE
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks"
    )
endif ()

# ---------------------------------------------------------------------------
# 测试：可选的 ctest 集成。实际测试逻辑由 tests/run_tests.sh 驱动，
# 这里只是让 `ctest` 也能触发，方便接入 CI。
# ---------------------------------------------------------------------------
option(BUILD_TESTING "构建并注册测试" OFF)
if (BUILD_TESTING)
    enable_testing()
    add_test(NAME unit_tests
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh")
endif ()

# 构建目录中提供 resources，并创建兼容软链 muvik -> yiufplayer
add_custom_command(TARGET yiufplayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:yiufplayer>/resources"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "$<TARGET_FILE_NAME:yiufplayer>"
        "$<TARGET_FILE_DIR:yiufplayer>/muvik"
    COMMENT "Link resources and muvik compatibility alias into build directory"
)

# 合规文档随安装包分发
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LICENSE-LGPL-2.1-mpv.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LICENSE-Apache-2.0-borealis.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LICENSE-curl-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/LICENSE-MIT-quickjs.txt"
    DESTINATION share/doc/yiufplayer
)

# 安装配置 (Linux / SteamOS / Flatpak)
if (UNIX AND NOT APPLE)
    install(TARGETS yiufplayer DESTINATION bin)

    # 资源目录必须以绝对安装路径存在，否则运行时会找不到字体与图标。
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            DESTINATION share/yiufplayer
            USE_SOURCE_PERMISSIONS)

    # 桌面集成三件套的 ID 必须完全一致，否则桌面环境不认：
    #   · desktop 文件名     = <app-id>.desktop
    #   · desktop 的 Icon=    = <app-id>（图标文件名去掉 .png）
    #   · metainfo 的 launchable = <app-id>.desktop
    #
    # 之前这里是三套不同命名撞在一起：install 重命名成 org.yiuf.YiufPlayer.*，
    # 而 desktop 文件里写着 Icon=org.muvik.Muvik、metainfo 的 launchable 是
    # org.muvik.Muvik.desktop —— 装出来的桌面图标找不到图、AppStream 关联断裂。
    # 统一到 AppStream id 的既有值 org.muvik.Muvik（改名会破坏已发布的元数据
    # 与 Flathub 关联），只改 install 的 RENAME 与 desktop 的 Icon。
    install(FILES flatpak/org.muvik.Muvik.desktop
            DESTINATION share/applications
            RENAME org.muvik.Muvik.desktop)

    install(FILES resources/icon/icon.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME org.muvik.Muvik.png)

    install(FILES flatpak/org.muvik.Muvik.metainfo.xml
            DESTINATION share/metainfo)
endif ()
