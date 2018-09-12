// futexplugin.cpp - this is a proof of concept of a plugin system using
// an external process as a sandbox for the plugin and very fast communication
// with shared memory and futexes.

#include <cstdlib>
#include <iostream>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include "futexplugin.h"

int main(int argc, char* argv[]) {
  // We have just been forked from the client, after all, we are a plugin.
  // This means we still have an open file descriptor to a file which we
  // now want to memory map to have a shared map with the parent. The
  // file descriptor is our one and only command line argument:
  if (argc < 2) {
    std::cerr << "Plugin: need at least one command line argument."
              << std::endl;
    return 1;
  }

  int fd = atoi(argv[1]);
  std::cout << "Plugin: fd=" << fd << std::endl;

  // Create a shared anonymous mapping that will hold the futexes.
  // Since the futexes are being shared between processes, we
  // subsequently use the "shared" futex operations (i.e., not the
  // ones suffixed "_PRIVATE")
  void* addr = mmap(NULL, sizeof(SharedMem), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "Plugin: could not map shared memory region: "
              << strerror(errno) << std::endl;
    return 2;
  }

  SharedMem* shared = static_cast<SharedMem*>(addr);
  // no placement new, object is already initialized in the client!

  shared->serve();

  // And cleanup:
  munmap(addr, sizeof(SharedMem));

  return 0;
}
