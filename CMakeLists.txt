cmake_minimum_required(VERSION 3.16)
project(micromads C)

# C11 for CubeMX compatibility
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/micromads.c
    src/mm_zmtp.c
)

set(HEADERS
    inc/micromads.h
    inc/mm_types.h
    inc/mm_zmtp.h
)

# static library
add_library(micromads STATIC ${SOURCES} ${HEADERS})

target_include_directories(micromads PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(micromads PRIVATE lwip_target_or_interface)
include(GNUInstallDirs)
install(TARGETS micromads
    EXPORT micromadsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)


### configure build in /usr/local
# cmake -Bbuild -DCMAKE_BUILD_TYPE=Release
### compilation
# cmake --build build -j4
### install
#sudo cmake --install build