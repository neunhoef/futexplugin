// futexplugin.cpp - this is a proof of concept of a plugin system using
// an external process as a sandbox for the plugin and very fast communication
// with shared memory and futexes.

#include <cstdlib>
#include <iostream>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <algorithm>

#include "futexplugin.h"

int main(int argc, char* argv[]) {
  long nloops = (argc > 1) ? atoi(argv[1]) : 3;

  // Create a shared mapping backed by a file that will hold the futexes.
  // Since the futexes are being shared between processes, we
  // subsequently use the "shared" futex operations (i.e., not the
  // ones suffixed "_PRIVATE")
  unlink("shared.map");
  int fd = open("shared.map", O_CREAT | O_RDWR | O_TRUNC, 0644);
  // unlink("shared.map");
  if (fd < 0) {
    std::cerr << "Client: could not create file 'shared.map': "
              << strerror(errno) << std::endl;
    return 1;
  }
  char buf[4096];
  memset(buf, 1, 4096);
  ssize_t len = write(fd, buf, 4096);
  if (len < 4096) {
    std::cerr << "Client: could not write buffer to file 'shared.map': "
              << strerror(errno) << std::endl;
    close(fd);
    return 1;
  }
  void* addr = mmap(NULL, sizeof(SharedMem), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "Could not allocated shared memory region: "
              << strerror(errno) << std::endl;
    return 1;
  }

  SharedMem* shared = new (addr) SharedMem();    // placement new

  // Now fork, the child will be the server/plugin:
  pid_t childPid = fork();
  if (childPid == -1) {
    std::cerr << "Could not fork: " << strerror(errno) << std::endl;
    munmap(addr, sizeof(SharedMem));    // just for cleanliness
    close(fd);
    return 2;
  }

  if (childPid == 0) {   // the child, start the service
    std::string fdst = std::to_string(fd);
    if (execl("./futexplugin_server",
              "./futexplugin_server", fdst.c_str()) < 0) {
      std::cerr << "Could not execl: " << strerror(errno) << std::endl;
      munmap(addr, sizeof(SharedMem));
      close(fd);
      return 2;
    }
    exit(0);
  }

  std::chrono::high_resolution_clock clock;
  auto startTime = clock.now();

  // The parent can move on from here and use the service:
  for (long j = 0; j < nloops; ++j) {
    double result = shared->call(static_cast<double>(j));
    if (result != j*j + 17.0) {
      std::cerr << "Alarm: wrong value for " << j << ": " << result
                << std::endl;
    }
  }

  auto endTime = clock.now();
  auto duration
    = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime-startTime);
  std::cout << "Time for " << nloops << " requests was " << duration.count()
            << " ns, that is, "
            << (long) (nloops / (duration.count() / 1000000000.0))
            << " reqs/s.\n"
            << "Server sleeps so far: " << shared->serverSleeps << "\n"
            << "Client sleeps so far: " << shared->clientSleeps << std::endl;

  // Now measure latency of single, timewise spaced out requests:
  std::this_thread::sleep_for(std::chrono::seconds(1));

  size_t storeServerSleeps = shared->serverSleeps;
  size_t storeClientSleeps = shared->clientSleeps;
  std::vector<uint64_t> times;
  times.reserve(100);
  for (int j = 0; j < 1000; ++j) {
    startTime = clock.now();
    double result = shared->call(static_cast<double>(j));
    endTime = clock.now();
    if (result != j*j + 17.0) {
      std::cerr << "Alarm: wrong value for " << j << ": " << result
                << std::endl;
    }
    duration 
      = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime-startTime);
    times.push_back(duration.count());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // This should put the server to sleep
  }
  std::sort(times.begin(), times.end());
  std::cout << "Latency in 1000 separate runs:\n"
            << "  smallest       : " << times[0] << " ns\n"
            << "  median         : " << (times[499]+times[500])/2 << " ns\n"
            << "  90%ile         : " << times[900] << " ns\n"
            << "  95%ile         : " << times[950] << " ns\n"
            << "  99%ile         : " << times[990] << " ns\n"
            << "  largest        : " << times[999] << " ns\n"
            << "  server sleeps  : " << shared->serverSleeps - storeServerSleeps
            << "\n"
            << "  client sleeps  : " << shared->clientSleeps - storeClientSleeps
            << std::endl;

  // Stop child:
  shared->stop();

  // Wait for the child to terminate:
  int returnValue = 0;
  waitpid(childPid, &returnValue, 0);

  // And cleanup:
  shared->~SharedMem();
  munmap(addr, sizeof(SharedMem));

  return 0;
}
