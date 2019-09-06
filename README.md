See the report here: [report.pdf](https://github.com/CharryWu/mini-dropbox-file-storage-system/blob/master/report.pdf)
# A mini-dropbox file storage service
## Overview
In PA 2, a client had only one blockstore and one metadata store to choose from, and so if either of those services were to fail, the entire cloud-based storage system would fail. In this project, you are going to deploy multiple block store services, and each block of a file can be stored in one or more of these services. This means that if one block replica is lost, there is one or more “backups” available, so the data isn’t lost. Further, clients now have a choice of more than one blockstore to access a particular block, and so the performance of the system can be improved by accessing blockstores that are “closer” to the client (and thus have less latency).

In particular, you are going to provision VMs on the Amazon Cloud (AWS), and run a BlockStore server process in each one. You should use these datacenters:

*   Seoul, Korea
*   Dublin, Ireland
*   Sao Paulo, Brazil
*   Mumbai, India

This means that there will be four servers running in your system. A client program can upload and download blocks from these servers, and will run on one of the four datacenters as well. So, for example, a client might run in Mumbai and download a block from the server running in Sao Paulo.

## The clients and the server

The focus of PA3 is on storing and retrieving blocks. In particular, PA3 is not going to involve file versions: each file will be created once, and then downloaded (perhaps multiple times). Files will not be deleted and they will not be modified. This is so that if you encountered any issues handling versioning in PA2, those issues will not affect your PA3 solution.

You will implement three applications: a SurfStoreServer, an uploader, and a downloader.

### SurfStoreServer

The server that you will run in each datacenter is the same as PA2 with one addition. The SurfStoreServer stores a fileinfo map and implements the simple get/put interface for blocks as before. After files are created they are never deleted or modified, so the version number for files will always be 1.

You will need to add a new RPC call to your SurfStoreServer that returns a list of all the blocks stored in that server. This is so that your downloader will know which blocks are stored where.

### Uploader

The uploader process is a program that uploads a set of files from a base directory to the cloud, using one of the block placement policies described below. The uploader program will process each file in the base directory. To process a file, the uploader will break the file into blocks, and store each block according to the the placement policy. Once the blocks for a file have been uploaded to the appropriate blockstore or blockstores, the uploader will insert a fileinfo entry for that file **into every SurfStoreServer**. This means that, after the uploader finishes processing a directory, that every SurfStoreServer will have the same fileinfo map, however different SurfStoreServers will have different sets of blocks.

If two different files happen to have one or more blocks in common, just treat each block separately. We’ll be working with binary files that have random data in them, and so it is extremely unlikely that two blocks will be in common.

## Block replication and location policies

You will experiment with two different block placement algorithms. For many applications, data tends to be used “near” where it is generated. For example, a user in California likely accesses their SurfStore folder from California most of the time. Thus having the blocks in a datacenter near the user would likely improve performance, even if they occasionally travel a large distance from their home.

Given a block B, we will consider the following five policies:

|Policy name|# copies of each block|Location of Block #1|Location of Block #2|
|-|-|-|-|
|random|1|random() mod N||
|tworandom|2|random() mod N|random() mod N (diff than block #1)|
|local|1|localhost||
|localclosest|2|localhost|Non-local server with min avg RTT|
|localfarthest|2|localhost|Non-local server with max avg RTT|

For the `random` policy, when a client uploads a file to the cloud, it simply chooses, for each block, a random datacenter and stores the block there. For `tworandom`, the client chooses two datacenters at random for each block, and stores one copy of the block in the first datacenter, and a second copy of that block in the second datacenter. Note for `tworandom`: make sure that you don’t store two copies of the same block on the same server–you must ensure that two different random datacenters are selected. For the `local` policy, the client simply stores all the blocks of the file on the blockstore running on the same datacenter as the client (localhost). There are two variants to the `local` policy. In the first, `localclosest`, the client stores two copies of the block–one on the local blockstore, and a second copy of that block on whichever other datacenter has the smallest average round-trip time (RTT) to the client. `localfarthest` is the opposite–the client stores a copy of the block on localhost, and a second copy of the block on the server that has the highest RTT from the client (i.e., is likely farthest away).

### Computing avg RTT

On the Internet, the round-trip time between two points is constantly changing based on network conditions, cross-traffic, congestion, and other network phenomenon. So your uploader program is going to compute, for every datacenter, the average of _eight_ round-trip time measurements. To estimate the round-trip time, simply time the latency of a `ping()` RPC call.

Your uploader program should print out the eight RTT measurements and the average RTT as you’ll need that data for your report.

## Examples

### random

![random policy](https://cseweb.ucsd.edu/~gmporter/classes/wi19/cse124/photos/pa3/Slide1.PNG)

### tworandom

![tworandom policy](https://cseweb.ucsd.edu/~gmporter/classes/wi19/cse124/photos/pa3/Slide2.PNG)

### local

![local policy](https://cseweb.ucsd.edu/~gmporter/classes/wi19/cse124/photos/pa3/Slide3.PNG)

### localclosest

![localclosest policy](https://cseweb.ucsd.edu/~gmporter/classes/wi19/cse124/photos/pa3/Slide4.PNG)

### localfarthest

![random policy](https://cseweb.ucsd.edu/~gmporter/classes/wi19/cse124/photos/pa3/Slide5.PNG)

### Uploader summary

The uploader program uploads blocks from files in a base directory to the SurfStoreServers based on the policy specified in the configuration file. After uploading the blocks for each file, the uploader sets the fileinfo entry for that file (with a version of 1) on every SurfStoreServer. After uploading all the files, the uploader exits and you can then begin your experiments.

### Downloader

The downloader program will download all of the files from the cloud to a local base directory. When the downloader starts, it:

1.  contacts the SurfStoreServer running on localhost to get the fileinfo map
2.  contacts each of the SurfStoreServers to request the list of blocks stored on those servers
3.  performs eight RTT measurements using the same methodology as the uploader program to compute the average RTT to each of the SurfStoreServers
4.  For each file, download the blocks for that file and reconstitute the file in the base directory. **The downloader should download the block from the SurfStoreServer with the _minimum_ average RTT.**

Your downloader program should print out the eight RTT measurements and the average RTT as you’ll need that data for your report.

Usage of these commands:

    $ ./ssd config.ini [servernum]
    $ ./uploader config.ini
    $ ./downloader config.ini

The format of the configuration file:

    [uploader]
    base_dir=base_uploader
    blocksize=4096
    policy=random

    [downloader]
    base_dir=base_downloader
    blocksize=4096

    [ssd]
    enabled=true
    num_servers=4
    server0=ec2-54-180-31-20.ap-northeast-2.compute.amazonaws.com:8001
    server1=ec2-52-16-48-1.eu-west-1.compute.amazonaws.com:8001
    server2=ec2-14-80-131-220.sa-south-2.compute.amazonaws.com:8001
    server3=ec2-206-109-13-41.in-southwest-1.compute.amazonaws.com:8001

**Note**: The specific IP addresses of your VMs will be assigned by Amazon, so you’ll have to edit the config file on each of the hosts with the correct values. Ensure that the configuration files on your nodes are all the same!

### Timing operations

For the experiments that you are going to run, your program needs to measure the time to download all of the files from the cloud. One option is to modify your downloader program to get the time when it starts, then download all the files, then get the time again and use the difference as your measurement. An alternative is to “wrap” the invocation of your downloader program with the `time` command:

    $ sleep 2
    $ time sleep 2

    real    0m2.020s
    user    0m0.000s
    sys     0m0.000s
    $

Note that printing out log messages slows down your program and can affect your results. So you will not want to print out a bunch of log messages during your experiment. Rather than removing the logging from your program, you can temporarily disable most or all of the log messages from being printed by adding the line:

    spdlog::set_level(spdlog::level::err);

immediately after the `initLogging();` command in your server’s `main()` method. Note that if you issue the above command, then to log the eight RTT measurements and average RTT value, you will need to log that value/measurement via:

    log->error("The average RTT is {}", avg_rtt);

## Experiments

### Experiment 1: single location analysis

*   for each policy P:
    *   for a datacenter D:
        1.  Run uploader on datacenter D
        2.  Run downloader on datacenter D
        3.  collect and store timing data for later analysis

You can choose which datacenter you’d like to use for D.

### Experiment 2: two location analysis

*   for each policy P:
    *   for a datacenter D1 and a datacenter D2 (where D1 != D2):
        1.  Run uploader on datacenter D1
        2.  Run downloader on datacenter D2
        3.  collect and store timing data for later analysis

For experiment 2, you can choose D1 and D2, however you should use the same D1 and D2 for each policy in experiment 2.

### Reminder

Make sure to restart your SurfStoreServers to clear out all their state between each part of the experiment. For example, after running the experiment on the `random` policy, make sure to restart the servers before running the `tworandom` experiment so that blocks from the first policy aren’t still there when you start the `tworandom` experiment.

## Lab report
See the report here: [report.pdf](https://github.com/CharryWu/mini-dropbox-file-storage-system/blob/master/report.pdf)  
You should write up a lab report (in PDF format) describing your results. It should include your names, student IDs, and github IDs. For each experiment, you should include the RTT values and average RTT observed by the uploader and by the downloader. You should also include a graph of the runtimes of each of the policies. Finally, you should write up a paragraph long explanation of your observations for each experiment.