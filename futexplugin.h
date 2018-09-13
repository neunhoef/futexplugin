// futexplugin.h - this is a proof of concept of a plugin system using
// an external process as a sandbox for the plugin and very fast communication
// with shared memory and futexes.

#include <chrono>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <cstdint>
#include <atomic>
#include <cstring>

namespace {

// The futex system call:

static int futex(int *uaddr, int futex_op, int val,
                 const struct timespec *timeout, int *uaddr2, int val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}

// For tight spinning loops:

inline void cpu_relax() {
#if defined(__i386) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
#if defined _WIN32
  YieldProcessor();
#else
  asm volatile("pause" ::: "memory");
#endif
#else
  static constexpr std::chrono::microseconds us0{0};
  std::this_thread::sleep_for(us0);
#endif
}

}  // namespace

// This is the memory layout for our shared memory communications:
struct SharedMem {

  enum State {
    SERVER_SPINNING = 0,
    SERVER_SLEEPING = 1,
    CLIENT_SPINNING = 2,
    CLIENT_SLEEPING = 3,
    WORK_DONE       = 4
  };

  /// I got confused twice because there is a function futex() and an
  ///  atomic object futex that is set via function calls.
  ///  Slightly different name maybe?  futexState?
  /// Yes, this should be changed.
  std::atomic<int32_t> futex;
     // This is initially SERVER_SPINNING, indicating that the server spins
     // and no work has been submitted yet. The server
     // is waiting for it to be set to CLIENT_SPINNING or CLIENT_SLEEPING
     // which is used to indicate that work has been submitted. The client
     // can spin for a while and then go to sleep, setting the value
     // to CLIENT_SLEEPING. When the server has finished the work, it
     // sets it to WORK_DONE and spins at least until the client has
     // set it to SERVER_SPINNING again to avoid a client waiting forever.
  double input;  // The input variable of our server.
  double output; // The output variable of our server.
  bool stopFlag; // flag to tell the server to exit
  size_t serverSleeps;   // number of sleeps in server
  size_t clientSleeps;   // number of sleeps in client

  SharedMem()
    : futex(0), input(0.0), output(0.0), stopFlag(false),
      serverSleeps(0), clientSleeps(0) {}

  static constexpr int spinCount = 10000;

  void serve(void) {
    while (true) {
      if (waitForWork()) {
        serverSleeps++;
      }
      /// stopFlag is within a tight loop, what if compiler
      ///  optimizes out its load?  need volatile or atomic.
      /// This is an interesting point, in practice, this does not seem
      /// to be necessary. I have intentionally made it so that the client
      /// thread sets the stopFlag and then has a thread synchronization
      /// point, since it submits work, in this case this is the discovery
      /// of the true in stopFlag. I had thought that this is good enough.
      /// After all, I also have not made input volatile or atomie, so 
      /// with the same argument you could say that this loop could load
      /// input outside of the loop! But so far I have failed to point
      /// my finger to any paragraph in the standard which would support
      /// this hypothesis. I think it is crucial that we answer this
      /// question once and for all, but how?
      if (stopFlag) {
        alertClient();
        return;
      }
      output = input * input + 17.0;
      alertClient();
    }
  }

  double call(double d) {
    input = d;
    alertServer();
    if (waitForResult()) {
      clientSleeps++;
    }
    double res = output;
    futex.store(SERVER_SPINNING, std::memory_order_relaxed);
    return res;
  }

  void stop() {
    stopFlag = true;
    alertServer();
    waitForResult();
  }

 private:

  /// nice wrappers.
  bool waitForWork() {
    return waitFor(CLIENT_SPINNING, CLIENT_SLEEPING,
                   SERVER_SPINNING, SERVER_SLEEPING);
  }

  bool waitForResult() {
    return waitFor(WORK_DONE, WORK_DONE, CLIENT_SPINNING, CLIENT_SLEEPING);
  }

  void alertServer() {
    fromTo(SERVER_SPINNING, SERVER_SLEEPING, CLIENT_SPINNING);
  }

  void alertClient() {
    fromTo(CLIENT_SPINNING, CLIENT_SLEEPING, WORK_DONE);
  }

  void fromTo(State fromSpinning, State fromSleeping, State to) {
    int32_t expect = fromSpinning;
    if (futex.compare_exchange_strong(expect, to,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
      return;
    }
    futex.store(to, std::memory_order_release);
    ::futex((int*) &futex, FUTEX_WAKE, 1, NULL, NULL, 0);
  }

  bool waitFor(State target1, State target2, State spinning, State sleeping) {
    bool slept = false;
    while (true) {
      int32_t current;
      int i = 0;
      while (true) {   // left by return or break
        current = futex.load(std::memory_order_acquire);
        if (current == target1 || current == target2) {
          return slept;
        }
        if (current == spinning) {
          // Note that current can be something else yet than our spinning
          // indicator, in this case we spin until it is spinning.
          if (++i >= spinCount) {
            break;
          }
        }
        ::cpu_relax();
      }
      if (futex.compare_exchange_strong(current, sleeping,
                                        std::memory_order_relaxed)) {
        slept = true;
        /// is this casting a 32 bit integer to 64 bit?
        ::futex((int*) &futex, FUTEX_WAIT, sleeping, NULL, NULL, 0);
      }
    }
  }
};
