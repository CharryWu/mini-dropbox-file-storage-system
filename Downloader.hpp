#ifndef DOWNLOADER_HPP
#define DOWNLOADER_HPP

#include <string>
#include <vector>

#include "inih/INIReader.h"
#include "rpc/client.h"

#include "SurfStoreTypes.hpp"
#include "logger.hpp"

using namespace std;

class Downloader
{
  public:
    Downloader(INIReader &t_config);

    void download();

    const uint64_t RPC_TIMEOUT = 10000; // milliseconds

  protected:
    INIReader &config;

    string base_dir;
    int blocksize;

    int num_servers;
    vector<string> ssdhosts;
    vector<int> ssdports;
    void create_file_from_blocklist(string filename, list<string>& blocks);
};

#endif // DOWNLOADER_HPP
