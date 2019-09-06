#include <sysexits.h>
#include <string>
#include <vector>
#include <iostream>
#include <assert.h>
#include <errno.h>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <limits>
#include <math.h>

#include <sys/types.h>
#include <dirent.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include "rpc/server.h"
#include "rpc/rpc_error.h"
#include "picosha2/picosha2.h"

#include "logger.hpp"
#include "Uploader.hpp"

using namespace std;
using namespace std::chrono;

float getstd(vector<int>& vec){
    float var = 0;
    float mean = accumulate(vec.begin(), vec.end(), 0.0)/vec.size(); 
    
    for(const auto& item : vec)
    {
      var += (item - mean) * (item - mean);
    }
    var /= vec.size();
    return sqrt(var);
}

/**
 * Helper function to calculated RTT for the given server
 *
 * Compute, for every datacenter, the average of eight round-trip time measurements.
 * To estimate the round-trip time, simply time the latency of a ping() RPC call.
 *
 * See https://stackoverflow.com/a/783872 for helper function naming
 * See https://www.geeksforgeeks.org/measure-execution-time-function-cpp/ for timing statement exec time
 */
float __calcSingleRTT(rpc::client * client, int index)
{
    auto log = logger();
    vector<int> durations;
    log->info("Calculating RTT for server #{}...", index);
    // compute, for every datacenter, the average of eight round-trip time measurements.
    for (int j = 0; j < 8; j++)
    {
        auto start = high_resolution_clock::now();
        client->call("ping"); // time the latency of a ping() RPC call for RTT
        auto stop = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(stop - start).count();
        durations.push_back(duration);
        log->error("RTT #{} = {}", j, duration);
    }
    float average = accumulate( durations.begin(), durations.end(), 0.0) / durations.size();
    log->error("Average RTT is {} for server #{}", average, index);
    log->info("RTT standard dev: {} milliseconds for server #{}", getstd(durations), index);

    return average;
}

Uploader::Uploader(INIReader &t_config)
    : config(t_config)
{
    auto log = logger();

    // Read in the uploader's base directory
    base_dir = config.Get("uploader", "base_dir", "");
    if (base_dir == "")
    {
        log->error("Invalid base directory: {}", base_dir);
        exit(EX_CONFIG);
    }
    log->info("Using base_dir {}", base_dir);

    // Read in the block size
    blocksize = (int)config.GetInteger("uploader", "blocksize", -1);
    if (blocksize <= 0)
    {
        log->error("Invalid block size: {}", blocksize);
        exit(EX_CONFIG);
    }
    log->info("Using a block size of {}", blocksize);

    // Read in the uploader's block placement policy
    policy = config.Get("uploader", "policy", "");
    if (policy == "")
    {
        log->error("Invalid placement policy: {}", policy);
        exit(EX_CONFIG);
    }
    log->info("Using a block placement policy of {}", policy);

    num_servers = (int)config.GetInteger("ssd", "num_servers", -1);
    if (num_servers <= 0)
    {
        log->error("num_servers {} is invalid", num_servers);
        exit(EX_CONFIG);
    }
    log->info("Number of servers: {}", num_servers);

    for (int i = 0; i < num_servers; ++i)
    {
        string servconf = config.Get("ssd", "server" + std::to_string(i), "");
        if (servconf == "")
        {
            log->error("Server {} not found in config file", i);
            exit(EX_CONFIG);
        }
        size_t idx = servconf.find(":");
        if (idx == string::npos)
        {
            log->error("Config line {} is invalid", servconf);
            exit(EX_CONFIG);
        }
        string host = servconf.substr(0, idx);
        int port = (int)strtol(servconf.substr(idx + 1).c_str(), nullptr, 0);
        if (port <= 0 || port > 65535)
        {
            log->error("Invalid port number: {}", servconf);
            exit(EX_CONFIG);
        }

        log->info("  Server {}= {}:{}", i, host, port);
        ssdhosts.push_back(host);
        ssdports.push_back(port);
    }

    log->info("Uploader initalized");
}

/**
 * The uploader process is a program that uploads a set of files from a base
 * directory to the cloud, using one of the block placement policies described below.
 * The uploader program will process each file in the base directory.
 * To process a file, the uploader will break the file into blocks, and store
 * each block according to the the placement policy. Once the blocks for a file
 * have been uploaded to the appropriate blockstore or blockstores, the uploader
 * will insert a fileinfo entry for that file into every SurfStoreServer.
 * This means that, after the uploader finishes processing a directory, that
 * every SurfStoreServer will have the same fileinfo map, however different
 * SurfStoreServers will have different sets of blocks.
 * If two different files happen to have one or more blocks in common, just
 * treat each block separately. We’ll be working with binary files that have
 * random data in them, and so it is extremely unlikely that two blocks will
 * be in common.
 */
void Uploader::upload()
{
    auto log = logger();

    vector<rpc::client *> clients;

    // Connect to all of the servers
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Connecting to server {}", i);
        try
        {
            clients.push_back(new rpc::client(ssdhosts[i], ssdports[i]));
            clients[i]->set_timeout(RPC_TIMEOUT);
        }
        catch (rpc::timeout &t)
        {
            log->error("Unable to connect to server {}: {}", i, t.what());
            exit(-1);
        }
    }

    // Issue a ping to each server
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Pinging server {}", i);
        try
        {
            clients[i]->call("ping");
            log->info("  success");
        }
        catch (rpc::timeout &t)
        {
            log->error("Error pinging server {}: {}", i, t.what());
            exit(-1);
        }
    }

    vector<float> avg_durations;

    // calc rtt for all servers
    for (int i = 0; i < num_servers; ++i)
    {
        avg_durations.push_back(__calcSingleRTT(clients[i], i));
    }

    // find local server index (element index with lowest value) and closest
    // server index (element index with second lowest value)
    int local_idx = -1, second_idx = -1;
    float smallest = numeric_limits<float>::max(), second = numeric_limits<float>::max();

    // find smallest and second smallest avg duration indices
    for( size_t i = 0; i < avg_durations.size(); ++i){
        if( avg_durations[i] <= smallest){
            second = smallest;
            smallest = avg_durations[i];
            second_idx = local_idx;
            local_idx = i;
        } else if( avg_durations[i] < second){
            second = avg_durations[i];
            second_idx = i;
        }
    } // end for

    int far_idx = max_element(avg_durations.begin(), avg_durations.end()) - avg_durations.begin();

    // The uploader program will process each file in the base directory.
    // To process a file, the uploader will break the file into blocks, and store
    // each block according to the the placement policy.
    DIR *dirp = opendir(base_dir.c_str());
    struct dirent *dp;
    while ((dp = readdir(dirp)) != NULL)
    {
        string filename = dp->d_name;

        // skip any file starting with .
        if (filename[0] == '.') { continue; }

        list<string> new_hashlist; // create a hashlist for each file
        list<string> blocks = get_blocks_from_file(filename);

        // for each file, compute that file’s hash list.
        for (const string &block : blocks)
        {
            string blockhash = picosha2::hash256_hex_string(block); // compute hash for each block
            new_hashlist.push_back(blockhash);
        }

        log->info("Uploading {} file blocks...", filename);

        bool block_upload_success = false;

        // The client should upload the blocks corresponding to this file to the server,
        // then update the server with the new FileInfo.
        // store each block according to the the placement policy.
        // can't use switch case: See https://stackoverflow.com/a/650218
        srand(time(NULL)); // initialize random seed with time
        if (policy == RAND)
        {
            block_upload_success = upload_data_rand(clients, new_hashlist, blocks);
        }
        else if (policy == TWO_RAND)
        {
            block_upload_success = upload_data_two_rand(clients, new_hashlist, blocks);
        }
        else if (policy == LOCAL)
        {
            block_upload_success = upload_data_local(clients, local_idx, new_hashlist, blocks);
        }
        else if (policy == LOCAL_CLOSE)
        {
            block_upload_success = upload_data_local_close(clients, local_idx, second_idx, new_hashlist, blocks);
        }
        else if (policy == LOCAL_FAR)
        {
            block_upload_success = upload_data_local_far(clients, local_idx, far_idx, new_hashlist, blocks);
        }

        if (!block_upload_success)
        {
            log->error("Fail uploading some blocks from file {}. Skip uploading its file info.", filename);
            continue;
        }

        // After files are created they are never deleted or modified,
        // so the version number for files will always be 1.
        FileInfo new_finfo = make_tuple(1, new_hashlist);

        // Once the blocks for a file
        // have been uploaded to the appropriate blockstore or blockstores, the uploader
        // will insert a fileinfo entry for that file into every SurfStoreServer.
        for (int i = 0; i < num_servers; ++i)
        {
            log->info("Uploading {} file info to server #{} ...", filename, i);
            bool finfo_update_success = clients[i]->call("update_file", filename, new_finfo).as<bool>(); // update the server with the new FileInfo.
            if (!finfo_update_success)
            {
                log->error("Fail updating {} file info. Skip.", filename);
            }
        } // end for uploading finfo to every server

    } // end while iterating over files in dir

    // Delete the clients
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Tearing down client {}", i);
        delete clients[i];
    }
}

/**
 * Get the data blocks from the file given by filename
 */
list<string> Uploader::get_blocks_from_file(string filename)
{
    auto log = logger();
    log->info("getting data blocks from file '{}'", filename);

    list<string> blocks;
    ifstream is(base_dir + "/" + filename, ifstream::binary); // read in file content

    if (!is)
    {
        // handle file permission error?
        log->error("error reading file '{}'", filename);
        return blocks;
    } // Sanity check: no permission or corrupt file

    char *blockbuffer = new char[blocksize]; // get each chunk of file content, chunksize = blocksize

    while (!is.eof())
    {
        is.read(blockbuffer, blocksize);        // only read in next blocksize bytes for each block
        string block(blockbuffer, is.gcount()); // support '\0' element in it
        blocks.push_back(block);
    }

    delete blockbuffer;
    return blocks;
}

/**
 * For the random policy, when a client uploads a file to the cloud, it simply
 * chooses, for each block, a random datacenter and stores the block there.
 */
bool Uploader::upload_data_rand(vector<rpc::client *> &clients, list<string> &hashlist, list<string> &blocklist)
{
    auto log = logger();
    bool block_upload_success = true;
    auto hashlist_it = hashlist.begin(); // same length as new_blocks
    auto blocks_it = blocklist.begin();  // same length as hashlist

    // iterate over each block
    while (hashlist_it != hashlist.end() && blocks_it != blocklist.end())
    {
        // it simply chooses, for each block, a random datacenter and stores the block there.
        int target_serv_id = rand() % num_servers;
        bool this_block_upload_success = clients[target_serv_id]->call("store_block", *hashlist_it, *blocks_it).as<bool>();

        if (!this_block_upload_success)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to server #{}. Skip.", *hashlist_it, target_serv_id);
        }

        ++hashlist_it; ++blocks_it;
    }
    return block_upload_success;
}

/**
 * For tworandom, the client chooses two datacenters at random for each block,
 * and stores one copy of the block in the first datacenter, and a second copy
 * of that block in the second datacenter. Note for tworandom: make sure that
 * you don’t store two copies of the same block on the same server–you must
 * ensure that two different random datacenters are selected.
 */
bool Uploader::upload_data_two_rand(vector<rpc::client *> &clients, list<string> &hashlist, list<string> &blocklist)
{
    auto log = logger();
    bool block_upload_success = true;
    auto hashlist_it = hashlist.begin(); // same length as new_blocks
    auto blocks_it = blocklist.begin();  // same length as hashlist

    // iterate over each block
    while (hashlist_it != hashlist.end() && blocks_it != blocklist.end())
    {
        int target_serv_id_1 = rand() % num_servers;
        int target_serv_id_2 = rand() % num_servers;

        // make sure two server ids are chose to store two copies
        while (target_serv_id_1 == target_serv_id_2)
        {
            target_serv_id_2 = rand() % num_servers;
        }

        bool this_block_upload_success_1 = clients[target_serv_id_1]->call("store_block", *hashlist_it, *blocks_it).as<bool>();
        bool this_block_upload_success_2 = clients[target_serv_id_2]->call("store_block", *hashlist_it, *blocks_it).as<bool>();

        if (!this_block_upload_success_1)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to first server #{}. Skip.", *hashlist_it, target_serv_id_1);
        }

        if (!this_block_upload_success_2)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to second server #{}. Skip.", *hashlist_it, target_serv_id_2);
        }

        ++hashlist_it; ++blocks_it;
    }

    return block_upload_success;
}

/**
 * For the local policy, the client simply stores all the blocks of the file on
 * the blockstore running on the same datacenter as the client (localhost).
 * just pick the server with the smallest RTT (don't check if it is equal to
 * zero because it might be non-zero (but close to zero) due to protocol overhead, etc.
 * See https://groups.google.com/a/ucsd.edu/forum/#!searchin/crs-cse124_wi19_a00-wi19/localhost|sort:date/crs-cse124_wi19_a00-wi19/kVkRrY5tYvg/5dHxsp4ABwAJ
 */
bool Uploader::upload_data_local(vector<rpc::client *> &clients, int local_idx, list<string> &hashlist, list<string> &blocklist)
{
    auto log = logger();
    bool block_upload_success = true;
    auto hashlist_it = hashlist.begin(); // same length as new_blocks
    auto blocks_it = blocklist.begin();  // same length as hashlist
    vector<float> avg_durations;

    // iterate over each block
    while (hashlist_it != hashlist.end() && blocks_it != blocklist.end())
    {
        bool this_block_upload_success = clients[local_idx]->call("store_block", *hashlist_it, *blocks_it).as<bool>();

        if (!this_block_upload_success)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to local server #{}. Skip.", *hashlist_it, local_idx);
        }

        ++hashlist_it; ++blocks_it;
    }
    return block_upload_success;
}

/**
 * For the localclosest policy, the client stores two copies of the block–one on
 * the local blockstore, and a second copy of that block on whichever other
 * datacenter has the smallest average round-trip time (RTT) to the client.
 */
bool Uploader::upload_data_local_close(vector<rpc::client *> &clients, int local_idx, int second_idx, list<string> &hashlist, list<string> &blocklist)
{
    auto log = logger();
    bool block_upload_success = true;
    auto hashlist_it = hashlist.begin(); // same length as new_blocks
    auto blocks_it = blocklist.begin();  // same length as hashlist

    // iterate over each block
    while (hashlist_it != hashlist.end() && blocks_it != blocklist.end())
    {
        bool this_block_upload_success_local = clients[local_idx]->call("store_block", *hashlist_it, *blocks_it).as<bool>();
        bool this_block_upload_success_second = clients[second_idx]->call("store_block", *hashlist_it, *blocks_it).as<bool>();

        if (!this_block_upload_success_local)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to local server #{}. Skip.", *hashlist_it, local_idx);
        }

        if (!this_block_upload_success_second)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to second closest server #{}. Skip.", *hashlist_it, second_idx);
        }

        ++hashlist_it; ++blocks_it;
    } // end while

    return block_upload_success;
}

/**
 * localfarthest is the opposite–the client stores a copy of the block on localhost,
 * and a second copy of the block on the server that has the highest RTT from
 * the client (i.e., is likely farthest away).
 */
bool Uploader::upload_data_local_far(vector<rpc::client *> &clients, int local_idx, int far_idx, list<string> &hashlist, list<string> &blocklist)
{
    auto log = logger();
    bool block_upload_success = true;
    auto hashlist_it = hashlist.begin(); // same length as new_blocks
    auto blocks_it = blocklist.begin();  // same length as hashlist

    // iterate over each block
    while (hashlist_it != hashlist.end() && blocks_it != blocklist.end())
    {
        bool this_block_upload_success_local = clients[local_idx]->call("store_block", *hashlist_it, *blocks_it).as<bool>();
        bool this_block_upload_success_far = clients[far_idx]->call("store_block", *hashlist_it, *blocks_it).as<bool>();
        if (!this_block_upload_success_local)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to local server #{}. Skip.", *hashlist_it, local_idx);
        }
        if (!this_block_upload_success_far)
        {
            block_upload_success = false;
            log->error("Fail uploading block with hash {} to furthest server #{}. Skip.", *hashlist_it, far_idx);
        }

        ++hashlist_it; ++blocks_it;
    }
    return block_upload_success;
}
