cmake_minimum_required(VERSION 3.10)
project(pm5-tui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find FTXUI package (this would be the external dependency)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(X11 REQUIRED x11)

# Try to find FTXUI
find_package(ftxui CONFIG)

# If not found, we'll need to build it or use a system version
if(NOT ftxui_FOUND)
    message(STATUS "FTXUI not found, attempting to find system package...")
    find_library(FTXUI_LIBRARY ftxui COMPONENTS screen component dom)
    if(FTXUI_LIBRARY)
        message(STATUS "Found FTXUI library at: ${FTXUI_LIBRARY}")
        add_library(ftxui::screen UNKNOWN IMPORTED)
        set_target_properties(ftxui::screen PROPERTIES IMPORTED_LOCATION ${FTXUI_LIBRARY})
        add_library(ftxui::component UNKNOWN IMPORTED)
        set_target_properties(ftxui::component PROPERTIES IMPORTED_LOCATION ${FTXUI_LIBRARY})
        add_library(ftxui::dom UNKNOWN IMPORTED)
        set_target_properties(ftxui::dom PROPERTIES IMPORTED_LOCATION ${FTXUI_LIBRARY})
    else()
        message(FATAL_ERROR "FTXUI library not found. Please install FTXUI or ensure it's in your system.")
    endif()
else()
    message(STATUS "Found FTXUI via find_package")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/RotatingFileLogger.cpp
)

# Header files
set(HEADERS
    src/RotatingFileLogger.h
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    src
)

# Link dependencies
target_link_libraries(${PROJECT_NAME} 
    PRIVATE
    ftxui::screen
    ftxui::component  
    ftxui::dom
)

# Set properties for the executable
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)