cmake_minimum_required(VERSION 3.16)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("video-compression" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(FFMPEG_DIR "C:/ffmpeg")
    include_directories(${FFMPEG_DIR}/include)
    link_directories(${FFMPEG_DIR}/lib)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
    include_directories(${FFMPEG_INCLUDE_DIRS})
    link_directories(${FFMPEG_LIBRARY_DIRS})
endif()

add_executable(video-compression
    "src/main.cpp"
    "src/VideoCompressor.cpp"
)

add_executable(ige-decode
    "src/ige_decode.cpp"
)

add_executable(test-codec
    "test/test_codec.cpp"
)

if(MSVC)
    target_compile_options(video-compression PRIVATE /W4)
    target_compile_options(ige-decode PRIVATE /W4)
    target_compile_options(test-codec PRIVATE /W4)
else()
    target_compile_options(video-compression PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ige-decode PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(test-codec PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(ige-decode PRIVATE src)
target_include_directories(test-codec PRIVATE src)

if(WIN32)
    target_link_libraries(video-compression avcodec avformat avutil swscale)
else()
    target_link_libraries(video-compression ${FFMPEG_LIBRARIES})
endif()

include(CTest)
add_test(NAME codec-unit COMMAND test-codec)
