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
set(ARK_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/Framework-ArkServerApi")
set(MYSQL_MODERN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/mysql-modern-cpp")
set(MARIADB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/MariaDB Connector C 64-bit")

# Include directories
include_directories(
    "${ARK_API_DIR}/include"
    "${ARK_API_DIR}/version/Core/Public"
    "patches/mysql+++"
    "${MYSQL_MODERN_DIR}/mysql+++"
    "${MARIADB_DIR}/include"
)

# Link directories
link_directories(
    "${ARK_API_DIR}/out_lib"
    "${MARIADB_DIR}/lib"
)

# Create the DLL
add_library(${PROJECT_NAME} SHARED src/dllmain.cpp)

# Link libraries
# Link libraries (Static MariaDB requires system libs)
target_link_libraries(${PROJECT_NAME}
    ArkApi
    mariadbclient
    ws2_32
    Secur32
    Shlwapi
    Crypt32
    Advapi32
    Bcrypt
)

# Set output names and directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "AsteroidBiomeChatNotifications"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/Release/AsteroidBiomeChatNotifications"
)

# Copy config files and create shared ZIP
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E tar "cf" "../AsteroidBiomeChatNotifications.zip" "--format=zip" "."
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)
