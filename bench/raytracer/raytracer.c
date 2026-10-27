/* raytracer.c - the Go/X11 ray tracer, ported to Crug.

   Same scene, same benchmark shape as raytracer_go.go: one Crug task
   ("goroutine") per pixel, 800x600 pixels, a result channel of capacity 8192,
   progressive display every 1024 completed pixels, and ESC as the only normal
   exit after rendering.  The render timing and the runtime/memory report at
   the end are kept from the Go program; the Go runtime counters
   (GOMAXPROCS, HeapAlloc, StackSys, GC cycles) become the Crug ones
   (worker count, task queue peak, channel counters, /proc/self/status).

   Build:
     make -C libcrug PAL=linux
     ./gcc/crugc -O2 -fcrug-goroutines -fcrug-channels -Ilibcrug/core \
       bench/raytracer/raytracer.c \
       libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o \
       libcrug/build/linux/switch.o libcrug/build/linux/stack.o \
       libcrug/build/linux/chan.o -pthread -lX11 -lm \
       -o build/raytracer-crug

   Run (needs an X display, like the Go original):
     ./build/raytracer-crug

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "chan.h" /* crug_chan_stats_get: the per-run channel counters  */
#include "task.h" /* crug_task_stats_get, crug_task_runtime_shutdown */

#define WIDTH 800
#define HEIGHT 600
#define PIXEL_COUNT (WIDTH * HEIGHT)
#define RESULT_BUFFER 8192
#define PRESENT_BATCH 1024
#define EPSILON 0.0001
#define CRUG_PI 3.14159265358979323846

/* ---- the X11 layer (the Go program's cgo preamble, written in C) -------- */

typedef struct XApp
{
  Display *display;
  int screen;
  Visual *visual;
  int depth;
  Window window;
  GC gc;
  Atom wm_delete;
  XImage *image;
  int stride;
  int byte_order;
  int lsb_first;
  uint64_t red_mask;
  uint64_t green_mask;
  uint64_t blue_mask;
} XApp;

enum
{
  EVENT_NONE = 0,
  EVENT_EXPOSE = 1,
  EVENT_CLOSE = 2,
  EVENT_ESCAPE = 3,
  EVENT_OTHER = 4
};

static void xapp_destroy (XApp *app);

static XApp *
xapp_create (int width, int height)
{
  XApp *app = calloc (1, sizeof *app);
  Window root;

  if (app == NULL)
    return NULL;

  app->display = XOpenDisplay (NULL);
  if (app->display == NULL)
    {
      free (app);
      return NULL;
    }

  app->screen = DefaultScreen (app->display);
  app->visual = DefaultVisual (app->display, app->screen);
  app->depth = DefaultDepth (app->display, app->screen);
  root = RootWindow (app->display, app->screen);

  app->window = XCreateSimpleWindow (app->display, root, 50, 50,
				     (unsigned int) width,
				     (unsigned int) height, 1,
				     BlackPixel (app->display, app->screen),
				     BlackPixel (app->display, app->screen));
  if (!app->window)
    {
      xapp_destroy (app);
      return NULL;
    }

  XStoreName (app->display, app->window,
	      "Crug Ray Tracer - 1 goroutine per pixel");
  XSelectInput (app->display, app->window,
		ExposureMask | KeyPressMask | StructureNotifyMask);

  app->wm_delete = XInternAtom (app->display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols (app->display, app->window, &app->wm_delete, 1);

  app->gc = XCreateGC (app->display, app->window, 0, NULL);
  if (!app->gc)
    {
      xapp_destroy (app);
      return NULL;
    }

  app->image = XCreateImage (app->display, app->visual,
			     (unsigned int) app->depth, ZPixmap, 0, NULL,
			     (unsigned int) width, (unsigned int) height, 32,
			     0);
  if (!app->image)
    {
      xapp_destroy (app);
      return NULL;
    }

  app->image->data = calloc (1, (size_t) app->image->bytes_per_line
				* (size_t) height);
  if (!app->image->data)
    {
      xapp_destroy (app);
      return NULL;
    }

  app->stride = app->image->bytes_per_line;
  app->byte_order = app->image->byte_order;
  app->lsb_first = LSBFirst;
  app->red_mask = (uint64_t) app->visual->red_mask;
  app->green_mask = (uint64_t) app->visual->green_mask;
  app->blue_mask = (uint64_t) app->visual->blue_mask;

  XMapWindow (app->display, app->window);
  XFlush (app->display);
  return app;
}

static void
xapp_destroy (XApp *app)
{
  if (app == NULL)
    return;

  if (app->image)
    {
      XDestroyImage (app->image);
      app->image = NULL;
    }
  if (app->display && app->gc)
    {
      XFreeGC (app->display, app->gc);
      app->gc = 0;
    }
  if (app->display && app->window)
    {
      XDestroyWindow (app->display, app->window);
      app->window = 0;
    }
  if (app->display)
    {
      XCloseDisplay (app->display);
      app->display = NULL;
    }
  free (app);
}

static void
xapp_present (XApp *app)
{
  XPutImage (app->display, app->window, app->gc, app->image, 0, 0, 0, 0,
	     (unsigned int) WIDTH, (unsigned int) HEIGHT);
  XFlush (app->display);
}

static int
xapp_translate_event (XApp *app, XEvent *event)
{
  if (event->type == Expose)
    return event->xexpose.count == 0 ? EVENT_EXPOSE : EVENT_OTHER;

  if (event->type == ClientMessage)
    return (Atom) event->xclient.data.l[0] == app->wm_delete ? EVENT_CLOSE
							     : EVENT_OTHER;

  if (event->type == KeyPress)
    {
      KeySym key = XLookupKeysym (&event->xkey, 0);

      if (key == XK_Escape)
	return EVENT_ESCAPE;
    }

  return EVENT_OTHER;
}

static int
xapp_poll_event (XApp *app)
{
  XEvent event;

  if (XPending (app->display) <= 0)
    return EVENT_NONE;

  XNextEvent (app->display, &event);
  return xapp_translate_event (app, &event);
}

static int
xapp_wait_event (XApp *app)
{
  XEvent event;

  XNextEvent (app->display, &event);
  return xapp_translate_event (app, &event);
}

/* ---- the scene ---------------------------------------------------------- */

typedef struct
{
  double x;
  double y;
  double z;
} vec3;

struct PixelResult
{
  uint32_t index;
  uint32_t rgb;
};

static vec3 camera = { 0.0, 1.45, -4.8 };
static vec3 sphere_center = { 0.0, 1.05, 2.2 };
static const double sphere_radius = 1.05;
static double aspect;
static double scale;

static vec3
v3 (double x, double y, double z)
{
  vec3 v = { x, y, z };
  return v;
}

static vec3
vadd (vec3 a, vec3 b)
{
  return v3 (a.x + b.x, a.y + b.y, a.z + b.z);
}

static vec3
vsub (vec3 a, vec3 b)
{
  return v3 (a.x - b.x, a.y - b.y, a.z - b.z);
}

static vec3
vmul (vec3 a, double s)
{
  return v3 (a.x * s, a.y * s, a.z * s);
}

static double
vdot (vec3 a, vec3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double
vlength (vec3 a)
{
  return sqrt (vdot (a, a));
}

static vec3
vnormalize (vec3 a)
{
  double l = vlength (a);

  if (l <= 0)
    return v3 (0.0, 0.0, 0.0);
  return vmul (a, 1.0 / l);
}

static vec3
vreflect (vec3 v, vec3 n)
{
  return vsub (v, vmul (n, 2.0 * vdot (v, n)));
}

static double
clamp01 (double x)
{
  if (x < 0)
    return 0;
  if (x > 1)
    return 1;
  return x;
}

static uint32_t
rgb8 (double r, double g, double b)
{
  uint32_t rr = (uint32_t) (clamp01 (r) * 255.0 + 0.5);
  uint32_t gg = (uint32_t) (clamp01 (g) * 255.0 + 0.5);
  uint32_t bb = (uint32_t) (clamp01 (b) * 255.0 + 0.5);

  return (rr << 16) | (gg << 8) | bb;
}

static bool
intersect_sphere (vec3 origin, vec3 dir, vec3 center, double radius,
		  double *t_out)
{
  vec3 oc = vsub (origin, center);
  double a = vdot (dir, dir);
  double b = 2.0 * vdot (oc, dir);
  double c = vdot (oc, oc) - radius * radius;
  double discriminant = b * b - 4.0 * a * c;
  double root, inv2a, t0, t1;

  if (discriminant < 0)
    return false;

  root = sqrt (discriminant);
  inv2a = 1.0 / (2.0 * a);

  t0 = (-b - root) * inv2a;
  if (t0 > EPSILON)
    {
      *t_out = t0;
      return true;
    }

  t1 = (-b + root) * inv2a;
  if (t1 > EPSILON)
    {
      *t_out = t1;
      return true;
    }

  return false;
}

static bool
intersect_plane (vec3 origin, vec3 dir, double plane_y, double *t_out)
{
  double t;

  if (fabs (dir.y) < EPSILON)
    return false;

  t = (plane_y - origin.y) / dir.y;
  if (t <= EPSILON)
    return false;

  *t_out = t;
  return true;
}

static bool
sphere_blocks_light (vec3 point, vec3 normal, vec3 light_position,
		     vec3 center, double radius)
{
  vec3 start = vadd (point, vmul (normal, EPSILON * 8.0));
  vec3 to_light = vsub (light_position, start);
  double light_distance = vlength (to_light);
  vec3 dir = vnormalize (to_light);
  double t;

  if (!intersect_sphere (start, dir, center, radius, &t))
    return false;

  return t < light_distance;
}

static vec3
shade (vec3 base, vec3 point, vec3 normal, vec3 cam, bool allow_sphere_shadow)
{
  vec3 light_position = v3 (-3.5, 6.0, -2.0);
  vec3 center = v3 (0.0, 1.05, 2.2);
  vec3 to_light = vnormalize (vsub (light_position, point));
  vec3 to_camera = vnormalize (vsub (cam, point));
  double diffuse = fmax (0.0, vdot (normal, to_light));
  bool shadowed = false;
  double light_scale;
  vec3 color;

  if (allow_sphere_shadow)
    shadowed = sphere_blocks_light (point, normal, light_position, center,
				    sphere_radius);

  light_scale = shadowed ? 0.18 : 0.18 + 0.82 * diffuse;
  color = vmul (base, light_scale);

  if (!shadowed)
    {
      vec3 incoming = vmul (to_light, -1.0);
      vec3 reflected = vreflect (incoming, normal);
      double spec = pow (fmax (0.0, vdot (reflected, to_camera)), 48.0);

      color.x += spec * 0.55;
      color.y += spec * 0.55;
      color.z += spec * 0.55;
    }

  return color;
}

static uint32_t
trace_pixel (uint32_t px, uint32_t py)
{
  double u = (2.0 * ((double) px + 0.5) / (double) WIDTH - 1.0) * aspect
	     * scale;
  double v = (1.0 - 2.0 * ((double) py + 0.5) / (double) HEIGHT) * scale;
  vec3 dir = vnormalize (v3 (u, v, 1.0));
  double sphere_t, plane_t;
  bool hit_sphere = intersect_sphere (camera, dir, sphere_center,
				      sphere_radius, &sphere_t);
  bool hit_plane = intersect_plane (camera, dir, 0.0, &plane_t);

  if (hit_sphere && (!hit_plane || sphere_t < plane_t))
    {
      vec3 point = vadd (camera, vmul (dir, sphere_t));
      vec3 normal = vnormalize (vsub (point, sphere_center));
      vec3 base = v3 (0.18, 0.45, 0.95);
      vec3 color = shade (base, point, normal, camera, false);

      return rgb8 (color.x, color.y, color.z);
    }

  if (hit_plane)
    {
      vec3 point = vadd (camera, vmul (dir, plane_t));
      int checker_x = (int) floor (point.x);
      int checker_z = (int) floor (point.z);
      bool white = ((checker_x + checker_z) & 1) == 0;
      vec3 base = white ? v3 (0.88, 0.88, 0.88) : v3 (0.10, 0.10, 0.12);
      vec3 normal = v3 (0.0, 1.0, 0.0);
      vec3 color = shade (base, point, normal, camera, true);
      double fog = clamp01 ((plane_t - 7.0) / 22.0);
      vec3 sky = v3 (0.46, 0.68, 0.94);

      color = vadd (vmul (color, 1.0 - fog), vmul (sky, fog));
      return rgb8 (color.x, color.y, color.z);
    }

  {
    double t = 0.5 * (dir.y + 1.0);
    vec3 bottom = v3 (0.78, 0.87, 1.0);
    vec3 top = v3 (0.18, 0.42, 0.78);
    vec3 sky = vadd (vmul (bottom, 1.0 - t), vmul (top, t));

    return rgb8 (sky.x, sky.y, sky.z);
  }
}

/* ---- pixel packing ------------------------------------------------------ */

static uint64_t
pack_component (uint8_t value, uint64_t mask)
{
  unsigned shift = 0;
  unsigned bits = 0;
  uint64_t field, max_value, scaled;

  if (mask == 0)
    return 0;

  while (((mask >> shift) & 1) == 0)
    shift++;

  field = mask >> shift;
  while ((field & 1) != 0)
    {
      bits++;
      field >>= 1;
    }

  max_value = bits >= 64 ? ~(uint64_t) 0 : ((uint64_t) 1 << bits) - 1;
  scaled = ((uint64_t) value * max_value + 127) / 255;

  return (scaled << shift) & mask;
}

static uint32_t
rgb_to_xpixel (const XApp *app, uint32_t rgb)
{
  uint8_t r = (uint8_t) ((rgb >> 16) & 0xff);
  uint8_t g = (uint8_t) ((rgb >> 8) & 0xff);
  uint8_t b = (uint8_t) (rgb & 0xff);

  return (uint32_t) (pack_component (r, app->red_mask)
		     | pack_component (g, app->green_mask)
		     | pack_component (b, app->blue_mask));
}

static void
xapp_set_pixel (XApp *app, uint32_t x, uint32_t y, uint32_t rgb)
{
  uint32_t pixel = rgb_to_xpixel (app, rgb);
  size_t offset = (size_t) y * (size_t) app->stride + (size_t) x * 4;
  unsigned char *slot = (unsigned char *) app->image->data + offset;

  if (app->byte_order == app->lsb_first)
    {
      slot[0] = (unsigned char) (pixel & 0xff);
      slot[1] = (unsigned char) ((pixel >> 8) & 0xff);
      slot[2] = (unsigned char) ((pixel >> 16) & 0xff);
      slot[3] = (unsigned char) ((pixel >> 24) & 0xff);
    }
  else
    {
      slot[0] = (unsigned char) ((pixel >> 24) & 0xff);
      slot[1] = (unsigned char) ((pixel >> 16) & 0xff);
      slot[2] = (unsigned char) ((pixel >> 8) & 0xff);
      slot[3] = (unsigned char) (pixel & 0xff);
    }
}

static void
xapp_pump_events (XApp *app)
{
  for (;;)
    {
      int ev = xapp_poll_event (app);

      if (ev == EVENT_NONE)
	return;

      switch (ev)
	{
	case EVENT_EXPOSE:
	  xapp_present (app);
	  break;

	case EVENT_CLOSE:
	  fprintf (stderr,
		   "Window close ignored; press ESC after rendering to exit.\n");
	  break;

	default:
	  /* ESC is deliberately ignored while rendering.  */
	  break;
	}
    }
}

static void
xapp_wait_for_escape (XApp *app)
{
  printf ("Render complete. Press ESC to exit.\n");
  fflush (stdout);

  for (;;)
    {
      int ev = xapp_wait_event (app);

      switch (ev)
	{
	case EVENT_ESCAPE:
	  return;

	case EVENT_EXPOSE:
	  xapp_present (app);
	  break;

	case EVENT_CLOSE:
	  fprintf (stderr, "Window close ignored; press ESC to exit.\n");
	  break;

	default:
	  break;
	}
    }
}

/* ---- the task ----------------------------------------------------------- */

/* One task per pixel: the body traces its pixel and sends the result to the
   main thread, which alone touches X11.  results is a channel handle, which
   is Send (the frame copies the handle, not the channel).  */
static void
render_pixel (chan struct PixelResult results, uint32_t x, uint32_t y)
{
  struct PixelResult r;

  r.index = y * WIDTH + x;
  r.rgb = trace_pixel (x, y);

  send (results, r);
}

/* ---- timing and memory -------------------------------------------------- */

static double
now_seconds (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

struct MemSnapshot
{
  unsigned long vm_peak_kb;
  unsigned long vm_size_kb;
  unsigned long vm_rss_kb;
};

static void
read_mem (struct MemSnapshot *m)
{
  memset (m, 0, sizeof *m);

#if defined(__linux__)
  {
    FILE *f = fopen ("/proc/self/status", "r");
    char line[256];

    if (f == NULL)
      return;

    while (fgets (line, sizeof line, f) != NULL)
      {
	if (strncmp (line, "VmPeak:", 7) == 0)
	  sscanf (line + 7, "%lu", &m->vm_peak_kb);
	else if (strncmp (line, "VmSize:", 7) == 0)
	  sscanf (line + 7, "%lu", &m->vm_size_kb);
	else if (strncmp (line, "VmRSS:", 6) == 0)
	  sscanf (line + 6, "%lu", &m->vm_rss_kb);
      }

    fclose (f);
  }
#endif
}

static double
mib (unsigned long kb)
{
  return (double) kb / 1024.0;
}

/* ---- the render loop ---------------------------------------------------- */

static void
render_frame (XApp *app, uint32_t *pixels)
{
  chan struct PixelResult results = channel (RESULT_BUFFER);
  struct crug_task_stats stats;
  struct crug_chan_stats chans;
  struct MemSnapshot mem_before, mem_after_spawn, mem_after;
  const uint32_t total = PIXEL_COUNT;
  uint32_t completed = 0;
  uint32_t since_present = 0;
  uint32_t progress_step = total / 10;
  uint32_t next_progress = progress_step;
  double render_start, spawn_start, spawn_end, first_result = -1.0;
  double all_pixels, render_end;

  read_mem (&mem_before);

  render_start = now_seconds ();
  xapp_present (app);

  spawn_start = now_seconds ();

  for (uint32_t y = 0; y < HEIGHT; y++)
    for (uint32_t x = 0; x < WIDTH; x++)
      go render_pixel (results, x, y);

  spawn_end = now_seconds ();
  read_mem (&mem_after_spawn);

  while (completed < total)
    {
      struct PixelResult p = recv (results);
      uint32_t x = p.index % WIDTH;
      uint32_t y = p.index / WIDTH;

      if (completed == 0)
	first_result = now_seconds ();

      pixels[p.index] = p.rgb;
      xapp_set_pixel (app, x, y, p.rgb);

      completed++;
      since_present++;

      if (since_present >= PRESENT_BATCH)
	{
	  xapp_present (app);
	  xapp_pump_events (app);
	  since_present = 0;
	}

      if (progress_step != 0 && completed >= next_progress)
	{
	  unsigned long percent = (unsigned long) completed * 100ul
				  / (unsigned long) total;

	  printf ("\rRendering: %3lu%%  (%u / %u pixels)", percent, completed,
		  total);
	  fflush (stdout);
	  next_progress += progress_step;
	}
    }

  all_pixels = now_seconds ();

  /* A final present makes the last incomplete batch visible.  */
  xapp_present (app);
  render_end = now_seconds ();
  read_mem (&mem_after);

  printf ("\rRendering: 100%%  (%u / %u pixels)\n", total, total);

  close (results);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  crug_chan_stats_get (&chans);

  printf ("\n");
  printf ("=== Crug render timing ===\n");
  printf ("tasks created              : %u\n", total);
  printf ("workers                    : %u\n", stats.workers);
  printf ("logical CPUs               : %ld\n", sysconf (_SC_NPROCESSORS_ONLN));
  printf ("goroutine spawn loop       : %.6f s\n", spawn_end - spawn_start);
  printf ("average spawn cost         : %.2f ns/task\n",
	  (spawn_end - spawn_start) * 1e9 / (double) total);
  printf ("first result received      : %.6f s\n", first_result - render_start);
  printf ("all pixel results received : %.6f s\n", all_pixels - render_start);
  printf ("end-to-end render time     : %.6f s\n", render_end - render_start);
  printf ("effective throughput       : %.0f pixels/s\n",
	  (double) total / (render_end - render_start));
  printf ("present batch              : %d pixels\n", PRESENT_BATCH);

  printf ("\n");
  printf ("=== Crug memory/runtime ===\n");
  printf ("VmSize before spawn        : %.2f MiB\n", mib (mem_before.vm_size_kb));
  printf ("VmSize after spawn         : %.2f MiB\n",
	  mib (mem_after_spawn.vm_size_kb));
  printf ("VmRSS after spawn          : %.2f MiB\n",
	  mib (mem_after_spawn.vm_rss_kb));
  printf ("VmSize after render        : %.2f MiB\n",
	  mib (mem_after.vm_size_kb));
  printf ("VmRSS after render         : %.2f MiB\n",
	  mib (mem_after.vm_rss_kb));
  printf ("VmPeak                     : %.2f MiB\n", mib (mem_after.vm_peak_kb));
  printf ("task queue peak            : %u\n", stats.queue_peak);
  printf ("tasks completed            : %llu\n",
	  (unsigned long long) stats.completed);
  printf ("tasks lost / corrupt       : %llu / %llu\n",
	  (unsigned long long) stats.lost,
	  (unsigned long long) stats.stack_corrupt);
  printf ("channel elements sent      : %llu\n",
	  (unsigned long long) chans.sent);
  printf ("channel sends that waited  : %llu\n",
	  (unsigned long long) chans.wait);
  printf ("=========================\n");
  printf ("\n");
}

int
main (void)
{
  XApp *app;
  uint32_t *pixels;

  aspect = (double) WIDTH / (double) HEIGHT;
  scale = tan ((58.0 * CRUG_PI / 180.0) * 0.5);

  app = xapp_create (WIDTH, HEIGHT);
  if (app == NULL)
    {
      fprintf (stderr, "cannot initialize X11 display/window\n");
      return 1;
    }

  if (app->image->bits_per_pixel != 32)
    {
      fprintf (stderr,
	       "unsupported XImage format: %d bits/pixel (benchmark expects 32)\n",
	       app->image->bits_per_pixel);
      xapp_destroy (app);
      return 1;
    }

  pixels = calloc ((size_t) PIXEL_COUNT, sizeof *pixels);
  if (pixels == NULL)
    {
      fprintf (stderr, "cannot allocate the pixel buffer\n");
      xapp_destroy (app);
      return 1;
    }

  printf ("Optimized Crug ray tracer\n");
  printf ("Resolution : %dx%d\n", WIDTH, HEIGHT);
  printf ("Pixel tasks: %d\n", PIXEL_COUNT);
  printf ("Rendering one Crug goroutine per pixel...\n");
  fflush (stdout);

  render_frame (app, pixels);

  xapp_wait_for_escape (app);

  xapp_destroy (app);
  free (pixels);
  return 0;
}
