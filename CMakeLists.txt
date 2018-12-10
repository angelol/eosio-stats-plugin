file(GLOB HEADERS "include/eosio/stats_plugin/*.hpp")
add_library( stats_plugin
             src/stats_plugin.cpp
             ${HEADERS} include/eosio/stats_plugin/stats_plugin.hpp)

target_link_libraries( stats_plugin chain_plugin eosio_chain appbase fc ${LIBMONGOCXX_STATIC_LIBRARIES} ${LIBBSONCXX_STATIC_LIBRARIES})
find_package(libbsoncxx-static REQUIRED)
find_package(libmongocxx-static REQUIRED)
target_include_directories( stats_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" PRIVATE ${LIBBSONCXX_STATIC_INCLUDE_DIRS} ${LIBMONGOCXX_STATIC_INCLUDE_DIRS})

target_compile_definitions(stats_plugin
    PRIVATE ${LIBMONGOCXX_STATIC_DEFINITIONS} ${LIBBSONCXX_STATIC_DEFINITIONS}
    )
eosio_additional_plugin(stats_plugin)
