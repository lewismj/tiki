cmake_minimum_required(VERSION 3.8)

## Build dependencies should generally be downloaded from git and built from source.
##
## However, some dependencies this is impractical or not available,
## i.e. Python & Doxygen, so use find_package.

set(CMAKE_CXX_STANDARD 20)


# Enable testing
enable_testing()

# Set policy CMP0135 to NEW to handle the DOWNLOAD_EXTRACT_TIMESTAMP option
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()


include(FetchContent)

FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG        v2.6.0
)

FetchContent_MakeAvailable(unity)