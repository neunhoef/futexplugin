# C++ plugin architecture prototype

This is the prototype for a C++ plugin architecture, which runs the
plugins in a separate process. So far, it only runs on Linux, since it
uses futexes for cross-process thread synchronization, but the same sort
of technology should work for other platforms as well.

## How to try it out

Simply do

    make

and then run the first prototype with

    ./futexplugin 10000000

to do 10000000 ping/pongs plus some latency test. The second prototype
achieves a proper separation of client and server code and is started
like this:

    ./futexplugin_client 10000000

The executable `futexplugin_server` is the plugin and is launched by
`futexplugin_client` with a `fork`/`execl` combination.

## How it works

The client creates a small file and `mmap`s it into its address space
with a shared mapping. It then launches the plugin in a separate process
via `fork`/`exec`. The file descriptor remains open across the
`fork` and a command line option tells the plugin which file descriptor
to use. In this way the plugin can map the same memory area in its address
space. In this way, the two processes can exchange POD data and do thread
synchronization via futexes, which explicitly allow cross process shared
memory maps.

Both processes implement a relatively simple synchronization protocol, which
involves some spinning to achieve high throughput. I have measured
consistently around 6M ping/pong exchanges per second with one thread
in each process. This suggests a round trip time of less than 200ns
if requests follow in quick succession. Furthermore, I have measured
latencies of approximately 30us (microseconds) in the median and 60us to
90us in the 99th percentile if a context switch has to happen. This is
still acceptable for most purposes.

