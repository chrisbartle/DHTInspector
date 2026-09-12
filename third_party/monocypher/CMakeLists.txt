# Vendored Monocypher; see README.md for provenance. Upstream code is built
# with warnings off: it is not ours to fix, and its warnings would drown out
# our own.
add_library(monocypher STATIC
    monocypher.c
    monocypher.h
    monocypher-ed25519.c
    monocypher-ed25519.h
)

target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(monocypher PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(monocypher PRIVATE /w)
else()
    target_compile_options(monocypher PRIVATE -w)
endif()
