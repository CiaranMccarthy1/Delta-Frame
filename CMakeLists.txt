# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("video-compression")

set(CMAKE_CXX_STANDARD 17)

# Set FFmpeg path
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

# Build the project
add_executable(video-compression 
    "src/main.cpp"
    "src/VideoCompressor.cpp"
)

# Link FFmpeg libraries
target_link_libraries(video-compression
        avcodec
        avformat
        avutil
        swscale
)
