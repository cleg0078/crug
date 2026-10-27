# Raytracer demo: one Crug task per pixel

A single-file Crug port of `raytracer_go.go`, the Go/X11 ray tracer that
spawns one goroutine per pixel.  The scene, the camera and the benchmark shape
are unchanged: 800x600 pixels, one task per pixel, a result channel of
capacity 8192, the main thread alone updates X11, a present every 1024
completed pixels, and ESC is the only normal exit after rendering.

The port is written to show the Crug constructs end to end, not to be clever:

* `go render_pixel (results, x, y);` queues a task per pixel.  The frame holds
  the channel handle (a channel is Send) and two integers.
* `chan struct PixelResult results = channel (8192);` is a channel of a struct
  *by value*: the element size is fixed when the channel is made, so a task
  sends 8 bytes and the main thread receives a copy.
* `recv` on the main thread parks in the PAL; `send` in a task yields to the
  scheduler.  Neither hands a worker to Xlib.
* X11 stays on the main thread (the Xlib connection is not Send, so this is
  enforced rather than merely intended), and every pixel goes through
  `xapp_set_pixel` / `xapp_present`.

The **stat section is kept** from the Go program: the spawn loop, the first
result, the last result, the end-to-end time, the effective throughput and the
present batch are printed in the same order.  The Go runtime counters are
translated to the Crug ones: workers instead of `GOMAXPROCS`, the ready-queue
high-water mark, the task/channel counters, and `/proc/self/status`
(VmSize/VmRSS/VmPeak) instead of `runtime.MemStats`.

## Build and run

```
make -C libcrug PAL=linux
./gcc/crugc -O2 -fcrug-goroutines -fcrug-channels -Ilibcrug/core \
  bench/raytracer/raytracer.c \
  libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o \
  libcrug/build/linux/switch.o libcrug/build/linux/stack.o \
  libcrug/build/linux/chan.o -pthread -lX11 -lm \
  -o build/raytracer-crug
DISPLAY=:0 ./build/raytracer-crug
```

The program needs the X11 headers and `-lX11`, exactly like the Go original
(which needs `libx11-dev` for cgo).  It exits 1 with
`cannot initialize X11 display/window` when no display is reachable.
`CRUG_TASKS_WORKERS=<1..64>` and `CRUG_TASKS_STACK=<bytes>` are read before
the first spawn, like any Crug program.

## Sample output

One run on a 20-CPU host with the default 8 workers:

```
Optimized Crug ray tracer
Resolution : 800x600
Pixel tasks: 480000
Rendering one Crug goroutine per pixel...
Rendering:  10%  (48000 / 480000 pixels) ...
Rendering: 100%  (480000 / 480000 pixels)

=== Crug render timing ===
tasks created              : 480000
workers                    : 8
logical CPUs               : 20
goroutine spawn loop       : 0.061681 s
average spawn cost         : 128.50 ns/task
first result received      : 0.062421 s
all pixel results received : 1.102185 s
end-to-end render time     : 1.103031 s
effective throughput       : 435164 pixels/s
present batch              : 1024 pixels

=== Crug memory/runtime ===
VmSize before spawn        : 9.17 MiB
VmSize after spawn         : 1146.83 MiB
VmRSS after spawn          : 126.93 MiB
VmSize after render        : 662.86 MiB
VmRSS after render         : 75.88 MiB
VmPeak                     : 3982.11 MiB
task queue peak            : 464331
tasks completed            : 480000
tasks lost / corrupt       : 0 / 0
channel elements sent      : 480000
channel sends that waited  : 57213
=========================
```

## Against the Go original

Both programs were run on the same host, on the same display, with the Go side
at `GOMAXPROCS=8` to match the 8 Crug workers:

| build | spawn loop | average spawn cost | end-to-end | throughput |
| --- | --- | --- | --- | --- |
| Go 1.26.8, `GOMAXPROCS=8` | 0.585 s | 1218 ns/task | 0.890 s | 539,077 pixels/s |
| Crug, 8 workers | 0.062-0.065 s | 128-136 ns/task | 0.97-1.07 s | 449-492k pixels/s |
| Crug, 1 worker | 0.047-0.056 s | 98-117 ns/task | 0.45-0.51 s | 946k-1.08M pixels/s |

The spawn loop is where the port wins: 480,000 task creations cost about 0.06 s
instead of 0.59 s, because a spawned task no longer materializes its stack at
spawn time (ADR 0008 A-3) and the frame is 16 bytes.  The end-to-end number at
8 workers is *not* a win, and the run at 1 worker is the honest baseline for
that: the current scheduler takes a global spin lock per dispatch and every
worker parks on a broadcast epoch, so adding workers past one makes this shape
slower, not faster.  That is a property of the runtime (see
`docs/crug/PERFORMANCE.md` Section 4.1), not of the port; the port keeps the
Go program's "spawn everything, then receive" shape on purpose.

The memory report also shows the shape: 480,000 queued tasks reserve about
1.1 GiB of address space before the receive loop drains them, and the peak is
about 4 GiB with 8 workers (each worker also commits a 64 KiB stack and keeps
the bounded stack/task caches).  Every stack is released by the time the
render ends, so the post-render VmSize is back down to the worker stacks.
