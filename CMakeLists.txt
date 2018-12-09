file(GLOB HEADERS "include/eosio/stats_plugin/*.hpp")
add_library( stats_plugin
             src/stats_plugin.cpp
             ${HEADERS} include/eosio/stats_plugin/stats_plugin.hpp)

target_link_libraries( stats_plugin chain_plugin eosio_chain appbase fc )
target_include_directories( stats_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

eosio_additional_plugin(stats_plugin)
