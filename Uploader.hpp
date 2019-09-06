#ifndef UPLOADER_HPP
#define UPLOADER_HPP

#include <string>
#include <vector>

#include "inih/INIReader.h"
#include "rpc/client.h"

#include "SurfStoreTypes.hpp"
#include "logger.hpp"

using namespace std;

class Uploader
{
  public:
    Uploader(INIReader &t_config);

    void upload();

    const uint64_t RPC_TIMEOUT = 10000; // milliseconds

  protected:
    INIReader &config;

    string base_dir;
    int blocksize;
    string policy; // See SurfStoreType.hpp: one of "random", "tworandom", "local", "localclosest", "localfarthest"

    int num_servers;
    vector<string> ssdhosts;
    vector<int> ssdports;

    // helper functions to get/set blocks to/from local files
    list<string> get_blocks_from_file(string filename);
    // upload functions of various policies
    bool upload_data_rand(vector<rpc::client *>& clients, list<string>& hashlist, list<string>& blocklist);
    bool upload_data_two_rand(vector<rpc::client *>& clients, list<string>& hashlist, list<string>& blocklist);
    bool upload_data_local(vector<rpc::client *> &clients, int local_idx, list<string> &hashlist, list<string> &blocklist);
    bool upload_data_local_close(vector<rpc::client *> &clients, int local_idx, int second_idx, list<string> &hashlist, list<string> &blocklist);
    bool upload_data_local_far(vector<rpc::client *> &clients, int local_idx, int far_idx, list<string> &hashlist, list<string> &blocklist);
};

#endif // UPLOADER_HPP
