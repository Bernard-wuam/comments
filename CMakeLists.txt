add_library(Comments INTERFACE)
add_library(Comments::Comments ALIAS Comments)

target_sources(
    Comments
    INTERFACE
    FILE_SET HEADERS TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/comments/comments.h
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/comments/commentsid.h
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/comments/postcomments.h
)

# find_package(boost_beast CONFIG REQUIRED)
# find_package(boost_charconv CONFIG REQUIRED)
# find_package(boost_mysql REQUIRED)

# if(NOT ServerUtilities::ServerUtilities)
# find_package(ServerUtilities CONFIG QUIET)

# if(NOT ServerUtilities_FOUND)
# include(FetchContent)
# FetchContent_Declare(
# ServerUtilities
# GIT_REPOSITORY https://github.com/Bernard-wuam/serverutilities.git
# GIT_TAG main
# )
# FetchContent_MakeAvailable(ServerUtilities)
# endif()
# endif()

# target_link_libraries(Comments INTERFACE Boost::beast Boost::mysql
# ServerUtilities::ServerUtilities)
install(
    TARGETS Comments
    EXPORT CommentsTargets
    FILE_SET HEADERS
)

install(
    EXPORT CommentsTargets
    DESTINATION share/cmake/comments
    NAMESPACE Upload::
)

include(CMakePackageConfigHelpers)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/commentsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CommentsConfig.cmake
    INSTALL_DESTINATION share/cmake/comments)

write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/CommentsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CommentsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CommentsConfigVersion.cmake
    DESTINATION share/cmake/comments
)