# This is an initial cache file for builds with sanitizers: sanitizers for
# undefined behavior and adresses.  Additionally, the typical NDEBUG definition
# is removed from the build to keep asserts.
#
set(CMAKE_BUILD_TYPE Release CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -pedantic -Werror -fsanitize=undefined -fsanitize=address" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE -O3 CACHE INTERNAL "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
