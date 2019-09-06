#include <sysexits.h>
#include <string>

#include "rpc/server.h"

#include "logger.hpp"
#include "SurfStoreTypes.hpp"
#include "SurfStoreServer.hpp"

SurfStoreServer::SurfStoreServer(INIReader &t_config, int t_servernum)
    : config(t_config), servernum(t_servernum)
{
    auto log = logger();

    // pull our address and port
    string serverid = "server" + std::to_string(servernum);
    string servconf = config.Get("ssd", serverid, "");
    if (servconf == "")
    {
        log->error("{} not found in config file", serverid);
        exit(EX_CONFIG);
    }
    size_t idx = servconf.find(":");
    if (idx == string::npos)
    {
        log->error("Config line {} is invalid", servconf);
        exit(EX_CONFIG);
    }
    port = (int)strtol(servconf.substr(idx + 1).c_str(), nullptr, 0);
    if (port <= 0 || port > 65535)
    {
        log->error("The port provided is invalid: {}", servconf);
        exit(EX_CONFIG);
    }
}

void SurfStoreServer::launch()
{
    auto log = logger();

    log->info("Launching SurfStore server");
    log->info("My ID is: {}", servernum);
    log->info("Port: {}", port);

    rpc::server srv(port);

    srv.bind("ping", []() {
        auto log = logger();
        log->info("ping()");
        return;
    });

    /**
     * a new RPC call to your SurfStoreServer that returns a list of all the
     * block hashes stored in that server. This is so that your downloader will know
     * which blocks are stored where.
     */
    srv.bind("get_all_blocks_hashlist", [&](){
        list<string> all_blocks_hashlist;
        for (auto const& element : hdm) {
            all_blocks_hashlist.push_back(element.first);
        }
        return all_blocks_hashlist;
    });

    /** Get a block for a specific hash
     * Accessing member variables inside a lambda:
     * https://groups.google.com/a/ucsd.edu/forum/#!searchin/crs-cse124_wi19_a00-wi19/get_block|sort:date/crs-cse124_wi19_a00-wi19/pd8Z6T3bAiU/0xHPyFNgAgAJ
     */
    srv.bind("get_block", [&](string hash) {

        auto log = logger();
        log->info("get_block() with hash {}", hash);

        auto it = hdm.find(hash); // map<string,string>::iterator

        if (it == hdm.end()) { // Sanity check: block with hash do not exist in hdm
            log->error("Block with hash {} do not exist. Stop.", hash);
            return string("");
        }

        return (it->second); // first: key, second: value
    });

    /** Stores block b in the key-value store, indexed by hash value h
     * It should store data into the hdm:HashDataMap field.
     * On the server, blocks and the FileInfoMap are kept in memory.
     * The files aren't "reconstituted" onto the server's file system at all.
     * The BlockStore service only knows about blocks–it doesn’t know anything
     * about how blocks relate to files.
     * For hash collisions, we don't have to handle that case for this project.
     */
    srv.bind("store_block", [&](string hash, string data) {
        auto log = logger();
        log->info("store_block() with hash {}", hash);

        // Use insert() instead of []. See https://stackoverflow.com/questions/326062/in-stl-maps-is-it-better-to-use-mapinsert-than
        auto ret = hdm.insert(pair<string,string>(hash,data));

        if (ret.second == false) {
            log->error("Duplicate block hash {} in hdm. Stop.", hash);
        }

        return ret.second;
    });

    // update the FileInfo entry for a given file
    /** update_file(): Updates the FileInfo values associated with a file stored in the cloud.
     * This method replaces the hash list for the file with
     * the provided hash list only if the new version number
     * is exactly one greater than the current version number.
     * Otherwise, and error is sent to the client telling them that the version
     * they are trying to store is not right (likely too old).
     */
    srv.bind("update_file", [&](string filename, FileInfo finfo) {
        int clientv = get<0>(finfo);
        //find the given file's fileinfo
        auto fimit = fim.find(filename);
        //can't find the file in the fim
        if (fimit == fim.end()) { // Sanity check: new entry in fim
            log->info("Creating new entry for file {} in fim", filename);
            fim[filename] = finfo;
            return true;
        }

        // Files will not be deleted and they will not be modified. After files
        // are created they are never deleted or modified, so the version number
        // for files will always be 1.
        if (clientv != 1) { // Sanity check: the provided version has to be exactly one
            log->error("The clientv {} is not exactly one for the file {}", clientv, filename);
            return false; // fail
        }

        log->info("Update the file {} successful", filename);
        fim[filename] = finfo; // the line of code that actually update FileInfoMap
        return true; // success
    });

    /** Download a FileInfo Map from the server
     * get_fileinfo_map(): Returns a map of the files stored in the SurfStore cloud service.
     * It simply returns the map that was built previously in other functions.
     * File blocks and the FileInfoMap are kept in the server’s memory.
     * The files, on the server, are never “reconstituted” onto the server’s actual filesystem.
     *  FileInfo file1;
        get<0>(file1) = 42;
        get<1>(file1) = {"h0", "h1", "h2"};

        FileInfo file2;
        get<0>(file2) = 20;
        get<1>(file2) = {"h3", "h4"};

        FileInfoMap fmap;
        fmap["file1.txt"] = file1;
        fmap["file2.dat"] = file2;
     */
    srv.bind("get_fileinfo_map", [&]() {
        auto log = logger();
        log->info("get_fileinfo_map()");

        return fim;
    });
    srv.run();
}
