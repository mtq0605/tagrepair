cmake_minimum_required(VERSION 3.20)
project(tagrepair LANGUAGES CXX)

# tagrepair —— 标签修复(独立程序)。Windows + MSVC。
#
#   cmake -S . -B build -DFFMPEG_ROOT=<FFmpeg 开发包>
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
#
# 要的只有这个文件夹自己,加一份 FFmpeg 开发包(用它的 include/ 与 bin/ 里的 DLL;FFmpeg 不编进来、也不放进仓库)。
# 也可以被别的工程用 add_subdirectory() 带上(对方先设好 FFMPEG_ROOT):
# 那时成品放哪由对方在 add_subdirectory 之前设 TAGREPAIR_OUTPUT_DIR,测试程序与拷运行库默认都不做。

if(NOT MSVC)
    message(FATAL_ERROR "tagrepair 只支持 Windows + MSVC(Visual Studio 2022 的工具链)")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TAGREPAIR_TOP_LEVEL ON)
else()
    set(TAGREPAIR_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------
# FFmpeg 开发包:${FFMPEG_ROOT}/include(头文件)与 ${FFMPEG_ROOT}/bin(avformat / avcodec / avutil / swresample 的 DLL)
# 没给 -DFFMPEG_ROOT 时:看环境变量 TAGREPAIR_FFMPEG_ROOT;也没有就报错。工程里不写任何别处的路径
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg 开发包的根目录(含 include/ 与 bin/)")
if(NOT FFMPEG_ROOT)
    if(NOT "$ENV{TAGREPAIR_FFMPEG_ROOT}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{TAGREPAIR_FFMPEG_ROOT}" tagrepair_ffmpeg_guess)
        set(FFMPEG_ROOT "${tagrepair_ffmpeg_guess}" CACHE PATH "FFmpeg 开发包的根目录(含 include/ 与 bin/)" FORCE)
    else()
        message(FATAL_ERROR "没有 FFmpeg 开发包:请传 -DFFMPEG_ROOT=<文件夹>(里面要有 include/ 与 bin/),或者设环境变量 TAGREPAIR_FFMPEG_ROOT")
    endif()
endif()
if(NOT EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFMPEG_ROOT=${FFMPEG_ROOT} 下找不到 include/libavcodec/avcodec.h,请检查路径")
endif()

if(NOT DEFINED TAGREPAIR_OUTPUT_DIR)
    set(TAGREPAIR_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")
endif()
if(NOT DEFINED TAGREPAIR_BUILD_TESTS)
    set(TAGREPAIR_BUILD_TESTS ${TAGREPAIR_TOP_LEVEL})
endif()
# 单独构建时把四个 FFmpeg 运行库拷到成品旁边:那个文件夹整个拿走就能用(拖文件上去 / 双击)
if(NOT DEFINED TAGREPAIR_COPY_FFMPEG)
    set(TAGREPAIR_COPY_FFMPEG ${TAGREPAIR_TOP_LEVEL})
endif()

# 每个目标都用的编译设置。源码是 UTF-8 无 BOM、含中文:不加 /utf-8 的话中文版 Windows 上 MSVC 按 CP936 读,字符串全坏。
# C / C++ 运行库静态链进来:成品不依赖 VC++ 可再发行组件
function(tagrepair_settings target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE /utf-8 /W3 /permissive-)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endfunction()

# ---------------------------------------------------------------------------
# tagrepair.exe
# ---------------------------------------------------------------------------
# FFmpeg 的 DLL 延迟加载:程序先自己按顺序找到它们在哪(src/probe.cpp 的 loadFfmpeg),找不到就说一句、退出码 3,
# 而不是一启动就被系统弹"缺少 DLL"。FFmpeg 开发包自带的 .lib 是 MinGW 式的导入库,MSVC 的 /DELAYLOAD 不认
# (LNK4199,照样变成启动时就要的普通导入),所以用到的那几十个函数在这里列出来,让 lib.exe 按 .def 生成 MSVC 式的导入库。
# swresample 本程序不直接调(是 avcodec 要用),只要知道它的文件名。
add_executable(tagrepair
    src/main.cpp src/interactive.cpp src/batch.cpp src/tagfile.cpp src/copy_flac.cpp src/probe.cpp src/sync_detect.cpp src/win_util.cpp
    src/runctl.cpp src/flac_meta.cpp src/synth_flac.cpp src/test_files.cpp src/selftest.cpp)
tagrepair_settings(tagrepair)
target_include_directories(tagrepair PRIVATE ${FFMPEG_ROOT}/include)
set(TAGREPAIR_IMPORTS_avformat avformat_alloc_context avformat_open_input avformat_find_stream_info av_find_best_stream
    avformat_close_input av_read_frame avio_alloc_context avio_context_free)
set(TAGREPAIR_IMPORTS_avcodec avcodec_find_decoder avcodec_alloc_context3 avcodec_parameters_to_context avcodec_open2
    avcodec_free_context avcodec_send_packet avcodec_receive_frame avcodec_get_name avcodec_descriptor_get
    av_packet_alloc av_packet_free av_packet_unref avcodec_find_encoder avcodec_send_frame avcodec_receive_packet)
set(TAGREPAIR_IMPORTS_avutil av_malloc av_free av_freep av_dict_get av_frame_alloc av_frame_free av_frame_unref
    av_get_packed_sample_fmt av_sample_fmt_is_planar av_log_set_level av_frame_get_buffer av_channel_layout_default)
set(TAGREPAIR_IMPLIBS)
set(TAGREPAIR_FFMPEG_DLLS)
foreach(lib avformat avcodec avutil swresample)
    file(GLOB tagrepair_dll "${FFMPEG_ROOT}/bin/${lib}-*.dll")
    if(NOT tagrepair_dll)
        message(FATAL_ERROR "在 ${FFMPEG_ROOT}/bin 下没找到 ${lib}-*.dll(tagrepair 运行时按这个文件名加载它)")
    endif()
    list(GET tagrepair_dll 0 tagrepair_dll)
    list(APPEND TAGREPAIR_FFMPEG_DLLS "${tagrepair_dll}")
    get_filename_component(tagrepair_dll "${tagrepair_dll}" NAME)
    string(TOUPPER "${lib}" tagrepair_up)
    target_compile_definitions(tagrepair PRIVATE "TAGREPAIR_${tagrepair_up}_DLL=L\"${tagrepair_dll}\"")
    if(DEFINED TAGREPAIR_IMPORTS_${lib})
        string(REPLACE ";" "\n    " tagrepair_exports "${TAGREPAIR_IMPORTS_${lib}}")
        set(tagrepair_def "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg_imports/${lib}.def")
        set(tagrepair_implib "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg_imports/tagrepair_${lib}.lib")
        file(CONFIGURE OUTPUT "${tagrepair_def}" CONTENT "LIBRARY \"${tagrepair_dll}\"\nEXPORTS\n    ${tagrepair_exports}\n" @ONLY)
        add_custom_command(OUTPUT "${tagrepair_implib}"
            COMMAND ${CMAKE_AR} /nologo /machine:x64 "/def:${tagrepair_def}" "/out:${tagrepair_implib}"
            DEPENDS "${tagrepair_def}" VERBATIM
            COMMENT "tagrepair:生成 ${tagrepair_dll} 的导入库(可延迟加载)")
        list(APPEND TAGREPAIR_IMPLIBS "${tagrepair_implib}")
        target_link_options(tagrepair PRIVATE "/DELAYLOAD:${tagrepair_dll}")
    endif()
endforeach()
add_custom_target(tagrepair_imports DEPENDS ${TAGREPAIR_IMPLIBS})
add_dependencies(tagrepair tagrepair_imports)
target_link_libraries(tagrepair PRIVATE ${TAGREPAIR_IMPLIBS} bcrypt delayimp)
set_target_properties(tagrepair PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                "${TAGREPAIR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${TAGREPAIR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${TAGREPAIR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${TAGREPAIR_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${TAGREPAIR_OUTPUT_DIR}")
if(TAGREPAIR_COPY_FFMPEG)
    add_custom_command(TARGET tagrepair POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TAGREPAIR_FFMPEG_DLLS} "${TAGREPAIR_OUTPUT_DIR}"
        COMMENT "tagrepair:把 FFmpeg 的四个运行库拷到成品旁边")
endif()
# 许可与声明跟着成品走:成品放到哪(单独构建的 bin,或者别的工程给的文件夹),旁边都有 LICENSE、NOTICE.txt、README.md
add_custom_command(TARGET tagrepair POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt" "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${TAGREPAIR_OUTPUT_DIR}"
    COMMENT "tagrepair:把 LICENSE、NOTICE.txt、README.md 拷到成品旁边")
message(STATUS "tagrepair:成品放在 ${TAGREPAIR_OUTPUT_DIR};FFmpeg 开发包 ${FFMPEG_ROOT}")

# ---------------------------------------------------------------------------
# 测试:纯函数的核对(不需要 FFmpeg)+ 真文件上的自检 + 把成品当黑盒跑的几条(tests/process_tests.cmake)
# ---------------------------------------------------------------------------
if(TAGREPAIR_BUILD_TESTS)
    add_executable(tagrepair_tests tests/unit_tests.cpp src/flac_meta.cpp src/synth_flac.cpp src/test_files.cpp src/win_util.cpp src/sync_detect.cpp)
    tagrepair_settings(tagrepair_tests)
    target_link_libraries(tagrepair_tests PRIVATE bcrypt)
    # 核对「别的源文件里不写给人看的字面文字」要读 src/ 下的源文件:这个工程自己的文件夹,配置时定下
    target_compile_definitions(tagrepair_tests PRIVATE "TAGREPAIR_SOURCE_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    if(TAGREPAIR_TOP_LEVEL)
        enable_testing()
        add_test(NAME unit COMMAND tagrepair_tests)
        add_test(NAME selftest COMMAND tagrepair selftest --ffmpeg-dir "${FFMPEG_ROOT}/bin")
        add_test(NAME process COMMAND ${CMAKE_COMMAND}
            "-DTOOL=$<TARGET_FILE:tagrepair>" "-DTESTS=$<TARGET_FILE:tagrepair_tests>"
            "-DFFMPEG_BIN=${FFMPEG_ROOT}/bin" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/process_tests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/process_tests.cmake")
    endif()
endif()
