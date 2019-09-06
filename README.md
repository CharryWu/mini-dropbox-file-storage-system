# dropbox storage

Dropbox-like storage service

## Dependencies

This project relies on four dependencies:

* inih
  * A header-only config file handle (https://github.com/jtilly/inih)
* spdlog
  * A fast thread-safe logging library (https://github.com/gabime/spdlog)
* rpclib
  * A simple c++ RPC library (http://rpclib.net/)
* picosha2
  * The PicoSHA2 SHA-256 hash generator (https://github.com/okdshin/PicoSHA2)

## Preparing the starter code for your environment

You must use the same compiler to build the rpc library as you use
for the rest of your project, otherwise you will get a series of errors.
Thus to start working on the project, you need to re-build the rpc
library.

On AWS, make sure to run:

$ sudo yum groupinstall "Development Tools"

to install the C++ compiler.

### To rebuild the library on the *ieng6* servers

1. cd dependencies/src
1. tar -xzf rpclib.tar.gz
1. cd rpclib
1. mkdir build
1. cd build
1. $PUBLIC/cmake-3.13.2-Linux-x86_64/bin/cmake ..
1. $PUBLIC/cmake-3.13.2-Linux-x86_64/bin/cmake --build .
  * (note the dot after --build)
1. cp librpc.a ../../../lib/

OK you should be set.  Proceed to testing your starter code.

### To rebuild the library on your own computer or on an AWS virtual machine

1. cd dependencies/src
1. tar -xzf cmake-3.13.2-Linux-x86_64.tar.gz
1. tar -xzf rpclib.tar.gz
1. cd rpclib
1. mkdir build
1. cd build
1. ../../cmake-3.13.2-Linux-x86_64/bin/cmake ..
1. ../../cmake-3.13.2-Linux-x86_64/bin/cmake --build .
  * (note the dot after --build)
1. cp librpc.a ../../../lib/

OK you should be set.  Proceed to testing your starter code.

## Testing the starter code

To ensure that your starter code is ready to go, cd to the "src" directory off
the main directory, and type "make".

Next start up four VMs, and edit the configuration file with the IP addresses
that were assigned to you by AWS.  To keep the code/configuration file in
sync, you can rely on github (push your changes to your repo, then pull
the changes on each of the VMs).

Log into each of four VMs, and start a server on each one.  You need to
tell the server which server number it is.  For example, log into
server0, and run

* ./ssd myconfig.ini 0

then log into server1 and run

* ./ssd myconfig.ini 1

Then from one of the VMs, run the uploader:

* ./uploader myconfig.ini

and the downloader

* ./downloader myconfig.ini

You should see a few of the basic RPC calls get executed without any error
messages.  If you see any kind of error message or error output, contact the
teaching staff.
