#include <sysexits.h>
#include <string>
#include <vector>
#include <iostream>
#include <assert.h>
#include <errno.h>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <time.h>
#include <math.h>
#include "rpc/server.h"
#include "rpc/rpc_error.h"
#include "picosha2/picosha2.h"

#include "logger.hpp"
#include "Downloader.hpp"

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

float __calcSingleRTT(rpc::client *client, int index)
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
    float average = accumulate(durations.begin(), durations.end(), 0.0 ) / durations.size();
    log->error("Average RTT is {} for server #{}...", average, index);
    log->info("RTT standard dev: {} milliseconds for server #{}", getstd(durations), index);

    return average;
}

void Downloader::create_file_from_blocklist(string filename, list<string> &blocks)
{
    auto log = logger();
    log->info("Reconstituting file '{}'", filename);
    std::ofstream out(base_dir + "/" + filename);

    for (const string &block : blocks)
    {
        out << block;
    }

    out.close();
    log->info("File '{}' reconstitution successful", filename);
}

Downloader::Downloader(INIReader &t_config)
    : config(t_config)
{
    auto log = logger();

    // Read in the downloader's base directory
    base_dir = config.Get("downloader", "base_dir", "");
    if (base_dir == "")
    {
        log->error("Invalid base directory: {}", base_dir);
        exit(EX_CONFIG);
    }
    log->info("Using base_dir {}", base_dir);

    // Read in the block size
    blocksize = (int)config.GetInteger("downloader", "blocksize", -1);
    if (blocksize <= 0)
    {
        log->error("Invalid block size: {}", blocksize);
        exit(EX_CONFIG);
    }
    log->info("Using a block size of {}", blocksize);

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

    log->info("Downloader initalized");
}

void Downloader::download()
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
    vector<list<string>> all_serv_hashlists;

    // calc rtt for all servers and get all their
    for (int i = 0; i < num_servers; ++i)
    {
        avg_durations.push_back(__calcSingleRTT(clients[i], i));

        log->info("Getting block hashlist from server #{}", i);
        all_serv_hashlists.push_back(clients[i]->call("get_all_blocks_hashlist").as<list<string>>());
    }

    // argsort - indieces contains index corresponding to sorted values in avg_durations
    vector<int> indices(avg_durations.size());
    size_t n(0);
    generate(begin(indices), end(indices), [&] { return n++; });

    sort(begin(indices), end(indices), [&](int i1, int i2) {
        return avg_durations[i1] < avg_durations[i2];
    });

    // get fim from localhost (closest server)
    log->info("Getting FileInfoMap from server #{}", indices[0]);
    FileInfoMap remote_index = clients[indices[0]]->call("get_fileinfo_map").as<FileInfoMap>();

    unsigned int total_duration = 0;

    // warning: three nested iterations to download all blocks!
    for(const auto& key_val : remote_index){
        //get the file name of the remote_index
        string remote_filename = key_val.first;
        FileInfo remote_fileinfo = key_val.second; // a tuple
        list<string> remote_hashlist = get<1>(remote_fileinfo);

        // download blocks
        list<string> blocks;

        auto start = high_resolution_clock::now(); // start the timer

        // for each block, download it from closest available server
        for (const string &hash : remote_hashlist) {

            // iterate through all available servers from closest to farthest
            // until a server containing the given block hash is found.
            for (size_t find_serv_idx = 0; find_serv_idx < indices.size(); ++find_serv_idx) {
                //get the closest hashlist so far
                list<string> &cur_serv_hashlist = all_serv_hashlists[indices[find_serv_idx]];
                auto hash_exists_it = find(cur_serv_hashlist.begin(),cur_serv_hashlist.end(),hash);

                // block found! mission complete!
                if (hash_exists_it != cur_serv_hashlist.end()) {
                    // hash is guaranteed to exist on server #find_serv_idx
                    blocks.push_back(clients[indices[find_serv_idx]]->call("get_block", hash).as<string>());
                    break;
                } // end if
            } // end finding closest server for current block
        } // end iterating all block hashes of current file

        auto stop = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(stop - start).count();

        log->error("Download time of file {} is {} milliseconds.", remote_filename, duration);

        total_duration += duration;
        create_file_from_blocklist(remote_filename, blocks);
    } // end iterating all files in fim

    log->error("Total download time is {} milliseconds.", total_duration);

    // Delete the clients
    for (int i = 0; i < num_servers; ++i)
    {
        log->info("Tearing down client {}", i);
        delete clients[i];
    }
}
