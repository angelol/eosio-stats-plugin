/**
 *  @file
 *  @copyright eosauthority - free to use and modify - see LICENSE.txt
 *  @copyright Angelo Laub
 *  Based on the awesome https://github.com/eosauthority/eosio-watcher-plugin
 */
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/stats_plugin/stats_plugin.hpp>

#include <fc/io/json.hpp>
#include <fc/network/url.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/signals2/connection.hpp>

#include <unordered_map>

#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/exception/logic_error.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>

using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::basic::kvp;
using bsoncxx::types::b_int32;
using bsoncxx::types::b_int64;
using bsoncxx::types::b_date;

namespace eosio {
static appbase::abstract_plugin &_stats_plugin =
    app().register_plugin<stats_plugin>();

using namespace chain;

class stats_plugin_impl {
public:
  /*
   * mongocxx::instance can only exist once per process
   * and it already exists due to the mongodb plugin (even * if it is not switched on)
   */
  // mongocxx::instance mongo_inst;
  fc::optional<mongocxx::pool> mongo_pool;
  string mongo_db_name;
  const std::string stats_table_name{"s"};
  const std::string g_stats_table_name{"g"};

  typedef uint32_t action_seq_t;

  typedef std::map<transaction_id_type, int32_t>
      action_queue_t;

  chain_plugin *chain_plug = nullptr;
  fc::optional<boost::signals2::scoped_connection> accepted_block_conn;
  fc::optional<boost::signals2::scoped_connection> applied_tx_conn;
  action_queue_t action_queue;

  // Returns act_sequence incremented by the number of actions checked.
  // So 1 (this action) + all the inline actions
  action_seq_t on_action_trace(const action_trace &act,
                               const transaction_id_type &tx_id,
                               action_seq_t act_sequence) {
    // ilog("on_action_trace - tx id: ${u}", ("u",tx_id));
    if( !(act.act.account == N(eosio) && act.act.name == N(onblock)) ) {
      action_queue[tx_id] += 1;
    }
    //~ ilog("Added to action_queue: ${u}", ("u",act.act));
    act_sequence++;

    for (const auto &iline : act.inline_traces) {
      //~ ilog("Processing inline_trace: ${u}", ("u",iline));
      act_sequence = on_action_trace(iline, tx_id, act_sequence);
    }

    return act_sequence;
  }

  void on_applied_tx(const transaction_trace_ptr &trace) {
    if (!trace->receipt) {
      return;
    }
    if(trace->receipt->status != transaction_receipt_header::executed) {
      return;
    }
    // If we later find that a transaction was failed before it's included in a block, remove its actions from the action queue
    if (trace->failed_dtrx_trace) {
      if (action_queue[trace->failed_dtrx_trace->id]) {
        action_queue.erase(action_queue.find(trace->failed_dtrx_trace->id));
        return;
      }
    }

    auto id = trace->id;
    //~ ilog("trace->id: ${u}",("u",trace->id));
    //~ ilog("action_queue.count(id): ${u}",("u",action_queue.count(id)));
    if (!action_queue[id]) {
      action_seq_t seq = 0;
      for (auto &at : trace->action_traces) {
        seq = on_action_trace(at, id, seq);
      }
      // ilog("Transaction contains ${u} action traces", ("u", seq));
    } else {
      // fork
      action_queue.erase(action_queue.find(id));
    }
  }

  void on_accepted_block(const block_state_ptr &block_state) {
    //~ ilog("on_accepted_block | block_state->block: ${u}",
    //("u",block_state->block));
    // ilog("block_num: ${u}", ("u",block_state->block->block_num));
    fc::time_point btime = block_state->block->timestamp;
    int32_t tx_count{0};
    int32_t action_count{0};
    int32_t cpu_usage_us{0};
    int32_t net_usage_words{0};

    transaction_id_type tx_id;

    //~ Process transactions from `block_state->block->transactions` because it
    // includes all transactions including deferred ones
    //~ ilog("Looping over all transaction objects in
    // block_state->block->transactions");
    for (const auto &trx : block_state->block->transactions) {
      if (trx.trx.contains<transaction_id_type>()) {
        //~ For deferred transactions the transaction id is easily accessible
        //~ ilog("===> block_state->block->transactions->trx ID: ${u}",
        //("u",trx.trx.get<transaction_id_type>()));
        tx_id = trx.trx.get<transaction_id_type>();
      } else {
        //~ For non-deferred transactions we have to access the txid from within
        // the packed transaction. The `trx` structure and `id()` getter method
        // are defined in `transaction.hpp`
        //~ ilog("===> block_state->block->transactions->trx ID: ${u}",
        //("u",trx.trx.get<packed_transaction>().id()));
        tx_id = trx.trx.get<packed_transaction>().id();
      }

      // Remove this matching tx from the action queue.
      // If a transaction comes in a future block, this method allows us to
      // preseve the
      // remaining action queue between blocks to pickup a transaction at a
      // later time when
      // it is eventually included in a block, in comparison to flushing the
      // remaining action
      // queue and potentially leaving some transactions never to be alerted on.
      if( trx.status == transaction_receipt_header::executed ) {
        const auto action_qeue_size = action_queue[tx_id];
        action_count += action_qeue_size;
        // auto itr = action_queue.find(tx_id);
        // ilog("deleting txid ${u}", ("u", tx_id));

        // ilog("TX ${u}", ("u", tx_id));
        // ilog("Has used ${u} us of cpu", ("u", trx.cpu_usage_us));
        cpu_usage_us += static_cast<int32_t>(trx.cpu_usage_us);
        net_usage_words += static_cast<int32_t>(trx.net_usage_words);
        tx_count += 1;
      }
    }
    
    action_queue.clear();


    // ilog("action_qeue_size: ${u}", ("u", action_queue));

    // ilog("Block Nr. ${u}", ("u", block_state->block->block_num()));
    // ilog("cpu_usage_us: ${u}", ("u", cpu_usage_us));
    // ilog("TX Count: ${u}", ("u", tx_count));
    // ilog("Action Count: ${u}", ("u", action_count));

    auto mongo_client = mongo_pool->acquire();
    auto &mongo_conn = *mongo_client;
    auto stats_table =
        mongo_conn[mongo_db_name][stats_table_name];
    const auto block_num =
        static_cast<int32_t>(block_state->block->block_num());
    std::chrono::microseconds microsec{btime.time_since_epoch().count()};
    const auto millisec =
        std::chrono::duration_cast<std::chrono::milliseconds>(microsec);
    

    auto doc = make_document(kvp("block_num", b_int32{block_num}),
                             kvp("actions", b_int32{action_count}),
                             kvp("transactions", b_int32{tx_count}),
                             kvp("cpu_usage_us", b_int32{cpu_usage_us}),
                             kvp("net_usage_words", b_int32{net_usage_words}),
                             kvp("time", b_date{millisec}));

    auto filter = make_document(kvp("block_num", b_int32{block_num}));

    mongocxx::options::update update_opts{};
    update_opts.upsert(true);

    /**
      * Use replace_one to make this operation fork- and replay-safe
      */
    if (!stats_table.replace_one(filter.view(), doc.view(), update_opts)) {
      ilog("Mongo Exception! Quitting...");
      app().quit();
    }
    

    // todo: handle forks (counter might become slightly inaccurate due to microforks)
    auto g_stats_table =
        mongo_conn[mongo_db_name][g_stats_table_name];
    g_stats_table.update_one(
      make_document(), 
      make_document( 
        kvp("$inc", 
          make_document(
            kvp("actions", b_int64{static_cast<int64_t>(action_count)}),
            kvp("transactions", b_int64{static_cast<int64_t>(tx_count)})
          )
        ) 
      ),
      update_opts
    );
    //~ ilog("Done processing block_state->block->transactions");
  }
};

stats_plugin::stats_plugin() : my(new stats_plugin_impl()) {}

stats_plugin::~stats_plugin() {}

void stats_plugin::set_program_options(options_description &,
                                       options_description &cfg) {
  cfg.add_options()(
      "stats-mongodb-uri,m", bpo::value<std::string>(),
      "MongoDB URI connection string, see: "
      "https://docs.mongodb.com/master/reference/connection-string/."
      " Default database 'eosstats' is used if not specified in URI."
      " Example: mongodb://127.0.0.1:27017/eosstats")
      ("stats-plugin-wipe-mongo", bpo::bool_switch()->default_value(false),
         "Required with --replay-blockchain, --hard-replay-blockchain, or --delete-all-blocks to wipe mongo db."
         "This option required to prevent accidental wipe of mongo db.");
}

void stats_plugin::mongo_init() {
  auto mongo_client = my->mongo_pool->acquire();
  auto &mongo_conn = *mongo_client;
  auto stats_table =
      mongo_conn[my->mongo_db_name][my->stats_table_name];
  if (stats_table.count(make_document()) == 0) {
    // this is our first run, database is still empty
    ilog("Installing mongodb indexes");
    stats_table.create_index(
        bsoncxx::from_json(R"xxx({ "block_num" : 1 })xxx"));
    stats_table.create_index(
        bsoncxx::from_json(R"xxx({ "transactions" : 1 })xxx"));
    stats_table.create_index(bsoncxx::from_json(R"xxx({ "actions" : 1 })xxx"));
    stats_table.create_index(bsoncxx::from_json(R"xxx({ "time" : 1 })xxx"));
  }
}
void stats_plugin::wipe_database() {
   ilog("stats plugin wipe_database");

   auto mongo_client = my->mongo_pool->acquire();
   auto &mongo_conn = *mongo_client;
   auto db = mongo_conn[my->mongo_db_name];

   db.drop();
   
   ilog("done wipe_database");
}

void stats_plugin::plugin_initialize(const variables_map &options) {
  try {
    EOS_ASSERT(options.count("stats-mongodb-uri") == 1,
               fc::invalid_arg_exception,
               "stats_plugin requires stats-mongodb-uri to be specified (e.g. "
               "\"mongodb://127.0.0.1:27017/eosstats\")!");

    string uri_str = options.at("stats-mongodb-uri").as<string>();
    ilog("connecting to ${u}", ("u", uri_str));
    mongocxx::uri uri = mongocxx::uri{uri_str};
    my->mongo_db_name = uri.database();
    if (my->mongo_db_name.empty()) {
      my->mongo_db_name = "eosstats";
    }
    my->mongo_pool.emplace(uri);
    
    if( options.at( "replay-blockchain" ).as<bool>() || options.at( "hard-replay-blockchain" ).as<bool>() || options.at( "delete-all-blocks" ).as<bool>() ) {
            if( options.at( "stats-plugin-wipe-mongo" ).as<bool>()) {
               wipe_database();
            } else {
               EOS_ASSERT( false, chain::plugin_config_exception, "--stats-plugin-wipe-mongo required with --replay-blockchain, --hard-replay-blockchain, or --delete-all-blocks"
                                 " --stats-plugin-wipe-mongo remove all eosstats collections from mongodb." );
            }
     }
       

    mongo_init();

    my->chain_plug = app().find_plugin<chain_plugin>();
    auto &chain = my->chain_plug->chain();
    my->accepted_block_conn.emplace(chain.accepted_block.connect([&](
        const block_state_ptr &b_state) { my->on_accepted_block(b_state); }));

    my->applied_tx_conn.emplace(chain.applied_transaction.connect(
        [&](const transaction_trace_ptr &tt) { my->on_applied_tx(tt); }));
  }
  FC_LOG_AND_RETHROW()
}

void stats_plugin::plugin_startup() { ilog("Stats plugin started"); }

void stats_plugin::plugin_shutdown() {
  my->applied_tx_conn.reset();
  my->accepted_block_conn.reset();
}
}
