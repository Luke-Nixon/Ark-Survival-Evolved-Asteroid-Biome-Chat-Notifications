cmake_minimum_required(VERSION 3.15) # 3.15+ required for CMAKE_MSVC_RUNTIME_LIBRARY
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
cmake_policy(SET CMP0091 NEW)

project(AsteroidBiomeChatNotifications)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE)

# Force PDB generation for Release
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

# Define paths
# No database paths. This plugin holds no database connection: announcements go
# through ArkCrossChat's exported CrossChat_Post, resolved at call time. See the
# header comment in src/dllmain.cpp for why that is the point rather than a
# shortcut.
set(ARK_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/Framework-ArkServerApi")

# Include directories
include_directories(
    "${ARK_API_DIR}/include"
    "${ARK_API_DIR}/version/Core/Public"
)

# Link directories
link_directories(
    "${ARK_API_DIR}/out_lib"
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED src/dllmain.cpp)

# ArkApi and nothing else. The MariaDB client and the six system libraries it
# dragged in - ws2_32, Secur32, Shlwapi, Crypt32, Advapi32, Bcrypt - were all
# there to serve a single INSERT that no longer happens here.
target_link_libraries(${PROJECT_NAME}
    ArkApi
)

# Set output names and directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "AsteroidBiomeChatNotifications"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
)

# Package the release. NAMED FILES ONLY - never copy_directory over configs/.
#
# copy_directory sweeps in whatever is sitting there, and configs/config.json is
# the file an operator edits locally. The one in this repository has only ever
# held placeholders ("Pass": "password", against a database that does not
# exist), so nothing has leaked - but "it happened to be a template this time"
# is not a property worth relying on, and the plugin needs no credentials at all
# any more. Naming the files makes the safe outcome structural.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/configs/PluginInfo.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/PluginInfo.json"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/configs/config.example.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.example.json"
    # Remove any config.json this directory is still carrying from an earlier
    # build. Naming the copied files stops new ones arriving; it does nothing
    # about the one a previous copy_directory already left behind, and the
    # output directory is not cleaned between builds. Verified: the first build
    # after the packaging fix still had the old file sitting here.
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json"
    COMMAND ${CMAKE_COMMAND} -E tar "cf" "../AsteroidBiomeChatNotifications.zip" "--format=zip" "."
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)
