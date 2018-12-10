/**
 *  @file
 *  @copyright eosauthority - free to use and modify - see LICENSE.txt
 *  @copyright Angelo Laub 
 *  Based on the awesome https://github.com/eosauthority/eosio-watcher-plugin
 */
#pragma once
#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace eosio {

   using namespace appbase;

   typedef std::unique_ptr<class stats_plugin_impl> stats_plugin_ptr;

   class stats_plugin : public appbase::plugin<stats_plugin> {
   public:
      stats_plugin();
      virtual ~stats_plugin();

      APPBASE_PLUGIN_REQUIRES((chain_plugin))

      virtual void set_program_options(options_description&, options_description& cfg) override;

      void plugin_initialize(const variables_map& options);
      void plugin_startup();
      void plugin_shutdown();
      void mongo_init();

   private:
      stats_plugin_ptr my;
   };

}
