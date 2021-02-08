// futexplugin.cpp - this is a proof of concept of a plugin system using
// an external process as a sandbox for the plugin and very fast communication
// with shared memory and futexes.

#include <cstdlib>
#include <iostream>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <algorithm>

#include "futexplugin.h"

int main(int argc, char* argv[]) {
  long nloops = (argc > 1) ? atoi(argv[1]) : 3;

  // Create a shared anonymous mapping that will hold the futexes.
  // Since the futexes are being shared between processes, we
  // subsequently use the "shared" futex operations (i.e., not the
  // ones suffixed "_PRIVATE")

  /// shmget()/shmctl() would let you choose a random id that is shared
  ///  between applications.  Then your clients do not have to be the result
  ///  of a fork.  I have no idea if there is a Windows equivalent (been too long).
  /// I looked into this, this is indeed a possibility, for a plugin system
  /// (and indeed all the use cases I have currently in mind) this fork based
  /// approach seemed to be simpler. But I have worked with shmget and shmctl
  /// before and it works equally well. There is also something in boost,
  /// which gives one a shared memory segment in a platform independent way
  /// and we could use that.
  void* addr = mmap(NULL, sizeof(SharedMem), PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_SHARED, -1, 0);
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
    return 2;
  }

  if (childPid == 0) {   // the child, start the service
    shared->serve();
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
  /// serverSleeps and clientSleeps are neither atomic nor volatile.  How do you know
  ///  they are valid at this point instead of still cached in a CPU register?
  /// And I suspect the same is true for stopFlag.
  /// clientSleeps is only ever touched in the same thread in this program,
  /// so this should be fine, serverSleeps is incremented in the worker
  /// thread/process and read here, however, when the last request was finished
  /// a thread synchronization event has happened by an release/acquire pair
  /// on an atomic variable, this achieves a memory barrier and therefore
  /// my reading of the memory model is that the client thread should see
  /// all updates done in the other thread before that synchronization event.
  /// Anyway, both are only for diagnostic purposes (and show what 
  /// I expect! :-))
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
  /// why reserve 100 instead of 1000?
  /// bug, I first planned to do 100 and then went for 1000
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
