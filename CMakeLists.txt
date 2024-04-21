cmake_minimum_required(VERSION 3.2...3.5)
project(mbediso VERSION 0.0 LANGUAGES C)

set(MBEDISO_SRC
    src/internal/directory.c
    src/internal/fs.c
    src/internal/io.c
    src/internal/read.c
    src/internal/string_diff.c
    src/internal/util.c
)

add_library(mbediso STATIC ${MBEDISO_SRC})

target_include_directories(mbediso PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(mbediso PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

install(TARGETS mbediso
        LIBRARY DESTINATION "lib"
        ARCHIVE DESTINATION "lib"
        INCLUDES DESTINATION "include")

add_executable(read examples/main.c)
target_link_libraries(read PRIVATE mbediso)
