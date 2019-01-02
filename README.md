# EOSIO Stats Plugin
The eosio stats plugin can be used to gather statistics on the number of transactions and actions per block. Per-block data is written into a mongodb table for easy querying and analysis. 

## MongoDB Document Format 
The plugin gathers the following data for every block:
```
{
	"_id" : ObjectId("5c0ea0b980084e63d6219f57"),
	"actions" : 1986,
	"block_num" : 10614007,
	"transactions" : 1986,
	"cpu_usage_us" : 199880,
	"net_usage_words" : 25818,
	"time" : ISODate("2018-08-11T16:20:06Z")
}

```
Transactions is obviously the number of transactions in the block, no surprise there. 

The actions value is the number of individual actions (individual blockchain operations) including inline-actions.

The values cpu_usage_us and net_usage_words give the total usage of cpu and net resources of this block. The usage percentage of the chain can be calculated by comparing the value with "block_cpu_limit" or "block_net_limit" from cleos get info.

Time could be used to generate beautiful graphs or for traffic analysis.

The plugin automatically installs indexes on the columns "actions", "transactions", "block_num" and "time", so they can be queried against.

## Useful Queries

Get the block with the highest number of transactions:
```
db.s.find().sort({transactions: -1}).limit(1)
```

Get the block with the highest number of actions:
```
db.s.find().sort({actions: -1}).limit(1)
```

The transactions per seconds value (TPS) or actions per seconds (APS) can be calculated by taking number of transactions/actions in a block multiplied by 2.

## Analysis Tools
As a companion project, I built [eosstats](https://github.com/angelol/eosstats/) that provides a livestream via websocket of the activity in current blocks. It can also be used to feed information about your favourite eosio-based blockchain to [blocktivity](http://blocktivity.info/).

## Considerations while replaying the blockchain
When replaying the blockchain, please add the --stats-plugin-wipe-mongo flag to nodeos to make sure the action and transaction counters don't get messed up.

# Installation instructions

## Requirements
- Works on any EOSIO node that runs v1.4.0 and up.

## Building the plugin [Install on your nodeos server]
You need to statically link this plugin with nodeos. To do that, build eosio like that:
```
export LOCAL_CMAKE_FLAGS="-DEOSIO_ADDITIONAL_PLUGINS=<path-to-eosio-stats-plugin>"
./eosio_build.sh -s EOS

```
## Add to your nodeos config.ini 
```
plugin = eosio::stats_plugin
stats-mongodb-uri=mongodb://127.0.0.1:27017/eosstats

 ```
You need to have a mongodb server running at the given URI. The name of the database that should be used by the plugin can be given after the slash, "eosstats" in this example.

## Replay the blockchain
If you would like to gather block stats for historical blocks, the blockchain needs to be replayed after installation of the plugin.

## Confirm that it's working
While the blockchain is replaying, you can check progress like this:
```
mongo
use eosstats
db.s.find().sort({block_num: -1}).limit(1).pretty()
```
This query will show you the current block number.

