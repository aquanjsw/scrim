#define _GNU_SOURCE
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "wlr-layer-shell-unstable-v1-client.h"

/* ========================================================================== */
/*                               declarations                                 */
/* ========================================================================== */

#define MAX_OUTPUTS 64

struct alphastop {
  float stop;
  float alpha;
};

struct config {
  uint32_t height;
  uint32_t color;
  float overflow;
  struct alphastop alphastops[3];
  char **outputs;
  uint32_t noutputs;
};

struct output_state {
  struct state *state;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wl_output *output;
  uint32_t name;
  struct wl_list link;
};

struct state {
  bool initialized;
  struct config *cfg;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct zwlr_layer_shell_v1 *layer_shell;
  struct wl_list outputs;
};

static struct wl_buffer *draw(void *data, uint32_t width, uint32_t height);
static void create_scrim(struct state *state);

/* ========================================================================== */
/*                                 listeners                                  */
/* ========================================================================== */

/* ---------------------------- buffer listener ----------------------------- */

static void buffer_release(void *data, struct wl_buffer *buffer) {
  wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

/* --------------------------- registry listener ---------------------------- */

static void registry_handle_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version) {
  struct state *state = data;

  // versions are from niri 26.04
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 6);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 2);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    state->layer_shell =
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 5);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct output_state *ostate = calloc(1, sizeof(*ostate));
    ostate->state = state;
    ostate->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
    ostate->name = name;
    wl_list_insert(&state->outputs, &ostate->link);
  }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  struct state *state = data;
  struct output_state *ostate, *tmp;
  wl_list_for_each_safe(ostate, tmp, &state->outputs, link) {
    if (ostate->name == name) {
      if (ostate->layer_surface)
        zwlr_layer_surface_v1_destroy(ostate->layer_surface);
      if (ostate->surface)
        wl_surface_destroy(ostate->surface);
      wl_output_destroy(ostate->output);
      wl_list_remove(&ostate->link);
      free(ostate);
      break;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* ------------------------- layer surface listener ------------------------- */

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *layer_surface,
                                    uint32_t serial,
                                    uint32_t width,
                                    uint32_t height) {
  struct output_state *state = data;
  zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
  struct wl_buffer *buffer = draw(data, width, height);
  wl_surface_attach(state->surface, buffer, 0, 0);
  wl_surface_damage(state->surface, 0, 0, width, height);
  wl_surface_commit(state->surface);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *layer_surface) {
  struct output_state *ostate = data;
  zwlr_layer_surface_v1_destroy(layer_surface);
  ostate->layer_surface = NULL;
  wl_surface_destroy(ostate->surface);
  ostate->surface = NULL;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* --------------------------- output listener ------------------------------ */

static void output_listener_geometry(void *data,
                                     struct wl_output *output,
                                     int32_t x,
                                     int32_t y,
                                     int32_t physical_width,
                                     int32_t physical_height,
                                     int32_t subpixel,
                                     const char *make,
                                     const char *model,
                                     int32_t transform) {}

static void output_listener_scale(void *data,
                                  struct wl_output *output,
                                  int32_t factor) {}

static void output_listener_mode(void *data,
                                 struct wl_output *output,
                                 uint32_t flags,
                                 int32_t width,
                                 int32_t height,
                                 int32_t refresh) {}

static void output_listener_description(void *data,
                                        struct wl_output *output,
                                        const char *description) {}

static void output_listener_done(void *data, struct wl_output *output) {}

static void output_listener_name(void *data,
                                 struct wl_output *output,
                                 const char *name) {
  struct output_state *ostate = data;
  struct state *state = ostate->state;
  struct config *cfg = state->cfg;

  if (cfg->noutputs == 0)
    goto draw;

  bool match = false;
  for (int i = 0; i < cfg->noutputs; ++i) {
    if (strcmp(name, cfg->outputs[i]) == 0) {
      match = true;
      break;
    }
  }

  if (!match)
    return;

draw:
  ostate->surface = wl_compositor_create_surface(state->compositor);
  // No surface listener here as it follows layer_surface's lifecycle

  ostate->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      state->layer_shell, ostate->surface, ostate->output,
      ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "scrim");
  zwlr_layer_surface_v1_add_listener(ostate->layer_surface,
                                     &layer_surface_listener, ostate);

  zwlr_layer_surface_v1_set_anchor(ostate->layer_surface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(ostate->layer_surface, -1);
  zwlr_layer_surface_v1_set_size(ostate->layer_surface, 0,
                                 cfg->height * (1 + cfg->overflow));

  wl_surface_commit(ostate->surface);
}

static const struct wl_output_listener output_listener = {
    .description = output_listener_description,
    .name = output_listener_name,
    .done = output_listener_done,
    .scale = output_listener_scale,
    .mode = output_listener_mode,
    .geometry = output_listener_geometry,
};

/* ========================================================================== */
/*                                   miscs                                    */
/* ========================================================================== */

/* --------------------------------- draw ----------------------------------- */

static struct wl_buffer *draw(void *data, uint32_t width, uint32_t height) {
  struct output_state *ostate = data;
  struct state *state = ostate->state;
  struct config *cfg = state->cfg;
  int stride = width * 4;
  int pool_size = height * stride;

  int fd = memfd_create("scrim-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    perror("memfd_create");
    exit(1);
  }
  if (ftruncate(fd, pool_size) < 0) {
    perror("ftruncate");
    close(fd);
    exit(1);
  }
  if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) == -1) {
    perror("fcntl");
    close(fd);
    exit(1);
  }

  char *pool_data =
      mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, pool_size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  // gradient/fading scrim
  uint stop_row = height * cfg->alphastops[1].stop;
  uint32_t *rowptr;
  float delta_alphas[] = {
      255 * (cfg->alphastops[1].alpha - cfg->alphastops[0].alpha) /
          ((cfg->alphastops[1].stop - cfg->alphastops[0].stop) * height),
      255 * (cfg->alphastops[2].alpha - cfg->alphastops[1].alpha) /
          ((cfg->alphastops[2].stop - cfg->alphastops[1].stop) * height),
  };
  uint8_t alpha;
  uint8_t r = (cfg->color >> 16) & 0xFF;
  uint8_t g = (cfg->color >> 8) & 0xFF;
  uint8_t b = cfg->color & 0xFF;
  uint8_t pr, pg, pb;
  uint32_t color;
  for (uint row = 0; row != height; ++row) {
    rowptr = (uint32_t *)(pool_data + row * stride);
    if (row < stop_row) {
      alpha = 255 * cfg->alphastops[0].alpha + delta_alphas[0] * row;
    } else {
      alpha =
          255 * cfg->alphastops[1].alpha + delta_alphas[1] * (row - stop_row);
    }
    pr = (uint8_t)(r * alpha / 255);
    pg = (uint8_t)(g * alpha / 255);
    pb = (uint8_t)(b * alpha / 255);
    color = (alpha << 24) | (pr << 16) | (pg << 8) | pb;
    for (uint col = 0; col != width; ++col) {
      rowptr[col] = color;
    }
  }

  munmap(pool_data, pool_size);
  wl_buffer_add_listener(buffer, &buffer_listener, NULL);
  return buffer;
}

/* ---------------------------- argument parsing ---------------------------- */

static int parse_args(struct config *cfg, int argc, char **argv) {
  int c;
  struct option options[] = {
      {"height", required_argument, NULL, 'H'},
      {"color", required_argument, NULL, 'c'},
      {"overflow", required_argument, NULL, 'o'},
      {"alphas", required_argument, NULL, 'a'},
      {"outputs", required_argument, NULL, 'O'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0},
  };
  char *token;

  // clang-format off
  const char usage[] =
    "usage: scrim [OPTIONS]\n"
    "options:\n"
    "  -H, --height=HEIGHT set height (default: 60)\n"
    "  -c, --color=COLOR   set color in hex (default: #283034)\n"
    "  -o, --overflow=OVF  set overflow, final height: `height*(1+overflow)` (default: 2)\n"
    "  -a, --alphas=STOPS  set alpha stops in format `a0,a1,a2,s1`, s0 and s2 are\n"
    "                      implicitly 0 and 1 respectively (default: .8,.5,.0,.5)\n"
    "  -O, --outputs=OUTS  set output names to apply scrim to, comma-separated (default: all)\n"
    "  -h, --help          show this help message\n";
  // clang-format on

  while ((c = getopt_long(argc, argv, "H:c:o:a:O:h", options, NULL)) != -1) {
    if (c == -1)
      break;
    switch (c) {
      case 'H':
        cfg->height = atoi(optarg);
        break;
      case 'c':
        cfg->color = strtoul(optarg + 1, NULL, 16);  // with # prefix
        break;
      case 'o':
        cfg->overflow = strtof(optarg, NULL);
        break;
      case 'a':
        token = strtok(optarg, ",");
        cfg->alphastops[0].alpha = atof(token);
        token = strtok(NULL, ",");
        cfg->alphastops[1].alpha = atof(token);
        token = strtok(NULL, ",");
        cfg->alphastops[2].alpha = atof(token);
        token = strtok(NULL, ",");
        cfg->alphastops[1].stop = atof(token);
        break;
      case 'O':
        token = strtok(optarg, ",");
        while (token) {
          cfg->outputs[cfg->noutputs++] = strdup(token);
          token = strtok(NULL, ",");
        }
        break;
      case 'h':
        fprintf(stdout, "%s", usage);
        return 1;
      case '?':
      default:
        fprintf(stderr, "%s", usage);
        return -1;
    }
  };

  // checks
  if (cfg->height == 0) {
    fprintf(stderr, "Height must be greater than 0\n");
    return -1;
  }
  if (cfg->overflow < 0) {
    fprintf(stderr, "Overflow cannot be negative\n");
    return -1;
  }
  if (cfg->alphastops[0].stop >= cfg->alphastops[1].stop ||
      cfg->alphastops[1].stop >= cfg->alphastops[2].stop) {
    fprintf(stderr, "Alpha stops must be strictly increasing\n");
    return -1;
  }

  const char *debug = getenv("WAYLAND_DEBUG");
  if (debug && strcmp(debug, "1") == 0) {
    printf("Height: %u\n", cfg->height);
    printf("Color: #%06x\n", cfg->color);
    printf("Overflow: %f\n", cfg->overflow);
    printf("Alphastops: [%.2f, %.2f] [%.2f, %.2f] [%.2f, %.2f]\n",
           cfg->alphastops[0].stop, cfg->alphastops[0].alpha,
           cfg->alphastops[1].stop, cfg->alphastops[1].alpha,
           cfg->alphastops[2].stop, cfg->alphastops[2].alpha);
    printf("Outputs: ");
    if (cfg->noutputs == 0) {
      printf("all\n");
    } else {
      for (uint i = 0; i < cfg->noutputs; ++i) {
        printf("%s%s", cfg->outputs[i], i + 1 == cfg->noutputs ? "\n" : ", ");
      }
    }
  }

  return 0;
}

/* ========================================================================== */
/*                                   main                                     */
/* ========================================================================== */

int main(int argc, char *argv[]) {
  // parse args
  struct config cfg = {
      .height = 60,
      .color = 0x283034,
      .overflow = 2,
      .alphastops =
          {
              {0.0, 0.8},
              {0.5, 0.5},
              {1.0, 0.0},
          },
      .outputs = malloc(MAX_OUTPUTS * sizeof(char *)),
      .noutputs = 0,
  };
  switch (parse_args(&cfg, argc, argv)) {
    case -1:
      return 1;
    case 1:
      return 0;
    default:
      break;
  }

  // init states
  struct state state = {0};
  wl_list_init(&state.outputs);
  state.cfg = &cfg;
  state.initialized = false;

  // boilerplate
  struct wl_display *display = wl_display_connect(NULL);
  if (!display) {
    perror("Failed to connect to Wayland display");
    return 1;
  }

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &state);

  wl_display_roundtrip(display);

  struct output_state *ostate, *tmp;
  wl_list_for_each_safe(ostate, tmp, &state.outputs, link) {
    wl_output_add_listener(ostate->output, &output_listener, ostate);
  }

  while (wl_display_dispatch(display)) {
  }

  wl_registry_destroy(registry);
  wl_display_disconnect(display);
  return 0;
}
