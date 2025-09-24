#include "sdl_wrapper.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

const char WINDOW_TITLE[] = "CS 3";
const size_t WINDOW_WIDTH = 750;
const size_t WINDOW_HEIGHT = 500;
const SDL_Color SDL_BLACK = {0, 0, 0};
const int8_t FONT_HEIGHT_SCALE = 2;
const double MS_PER_S = 100000;

const size_t NUMBER_OF_SOUNDS = 5;

static Mix_Music *background_music = NULL;
static Mix_Chunk *gem_sound = NULL;
static Mix_Chunk *level_completed_sound = NULL;
static Mix_Chunk *level_failed_sound = NULL;
static Mix_Chunk *jump_sound = NULL;

/**
 * The coordinate at the center of the screen.
 */
vector_t center;
/**
 * The coordinate difference from the center to the top right corner.
 */
vector_t max_diff;
/**
 * The SDL window where the scene is rendered.
 */
SDL_Window *window;
/**
 * The renderer used to draw the scene.
 */
SDL_Renderer *renderer;
/**
 * The keypress handler, or NULL if none has been configured.
 */
key_handler_t key_handler = NULL;

/**
 * SDL's timestamp when a key was last pressed or released.
 * Used to mesasure how long a key has been held.
 */
uint32_t key_start_timestamp;
/**
 * The value of clock() when time_since_last_tick() was last called.
 * Initially 0.
 */
clock_t last_clock = 0;

/** Computes the center of the window in pixel coordinates */
vector_t get_window_center(void) {
  int *width = malloc(sizeof(*width)), *height = malloc(sizeof(*height));
  assert(width != NULL);
  assert(height != NULL);
  SDL_GetWindowSize(window, width, height);
  vector_t dimensions = {.x = *width, .y = *height};
  free(width);
  free(height);
  return vec_multiply(0.5, dimensions);
}

/**
 * Computes the scaling factor between scene coordinates and pixel coordinates.
 * The scene is scaled by the same factor in the x and y dimensions,
 * chosen to maximize the size of the scene while keeping it in the window.
 */
double get_scene_scale(vector_t window_center) {
  double x_scale = window_center.x / max_diff.x,
         y_scale = window_center.y / max_diff.y;
  return x_scale < y_scale ? x_scale : y_scale;
}

/** Maps a scene coordinate to a window coordinate */
vector_t get_window_position(vector_t scene_pos, vector_t window_center) {
  vector_t scene_center_offset = vec_subtract(scene_pos, center);
  double scale = get_scene_scale(window_center);
  vector_t pixel_center_offset = vec_multiply(scale, scene_center_offset);
  vector_t pixel = {.x = round(window_center.x + pixel_center_offset.x),
                    // Flip y axis since positive y is down on the screen
                    .y = round(window_center.y - pixel_center_offset.y)};
  return pixel;
}

/**
 * Converts an SDL key code to a char.
 * 7-bit ASCII characters are just returned
 * and arrow keys are given special character codes.
 */
char get_keycode(SDL_Keycode key) {
  switch (key) {
  case SDLK_LEFT:
    return LEFT_ARROW;
  case SDLK_UP:
    return UP_ARROW;
  case SDLK_RIGHT:
    return RIGHT_ARROW;
  case SDLK_DOWN:
    return DOWN_ARROW;
  case SDLK_SPACE:
    return SPACE_BAR;
  case SDLK_1:
    return KEY_1;
  case SDLK_2:
    return KEY_2;
  case SDLK_3:
    return KEY_3;
  case SDLK_h:
    return KEY_H;
  case SDLK_p:
    return KEY_P;
  case SDLK_r:
    return KEY_R;
  case SDLK_u:
    return KEY_U;
  default:
    // Only process 7-bit ASCII characters
    return key == (SDL_Keycode)(char)key ? key : '\0';
  }
}

void sdl_init(vector_t min, vector_t max) {
  // Check parameters
  assert(min.x < max.x);
  assert(min.y < max.y);

  center = vec_multiply(0.5, vec_add(min, max));
  max_diff = vec_subtract(max, center);
  SDL_Init(SDL_INIT_EVERYTHING);

  // initializing the music functionality
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    printf("%s\n", "music initialized");
    SDL_Log("Mix_OpenAudio: %s", Mix_GetError());
  }
  Mix_AllocateChannels(NUMBER_OF_SOUNDS);

  window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
                            SDL_WINDOW_RESIZABLE);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  TTF_Init();
}

bool sdl_is_done(state_t *state) {
  SDL_Event *event = malloc(sizeof(*event));
  assert(event != NULL);
  while (SDL_PollEvent(event)) {
    switch (event->type) {
    case SDL_QUIT:
      free(event);
      return true;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      // Skip the keypress if no handler is configured
      // or an unrecognized key was pressed
      if (key_handler == NULL)
        break;
      char key = get_keycode(event->key.keysym.sym);
      if (key == '\0')
        break;

      uint32_t timestamp = event->key.timestamp;
      if (!event->key.repeat) {
        key_start_timestamp = timestamp;
      }
      key_event_type_t type =
          event->type == SDL_KEYDOWN ? KEY_PRESSED : KEY_RELEASED;
      double held_time = (timestamp - key_start_timestamp) / MS_PER_S;
      key_handler(key, type, held_time, state);
      break;
    }
  }
  free(event);
  return false;
}

void sdl_clear(void) {
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderClear(renderer);
}

SDL_Rect sdl_get_body_bounding_box(body_t *body) {
  vector_t vert = {.x = __DBL_MAX__, .y = -__DBL_MAX__};
  vector_t horiz = {.x = -__DBL_MAX__, .y = __DBL_MAX__};
  list_t *points = body_get_shape(body);
  size_t n = list_size(points);
  vector_t window_center = get_window_center();
  for (size_t i = 0; i < n; i++) {
    vector_t *vertex = list_get(points, i);
    vector_t pixel = get_window_position(*vertex, window_center);
    if (pixel.y > vert.y) {
      vert.y = pixel.y;
    } else if (pixel.y < horiz.y) {
      horiz.y = pixel.y;
    }

    if (pixel.x > horiz.x) {
      horiz.x = pixel.x;
    } else if (pixel.x < vert.x) {
      vert.x = pixel.x;
    }
  }

  return (SDL_Rect){
      .x = vert.x, .y = horiz.y, .w = horiz.x - vert.x, .h = vert.y - horiz.y};
}

void sdl_draw_body(body_t *body) {
  // Check parameters
  list_t *points = body_get_shape(body);
  size_t n = list_size(points);
  assert(n >= 3);
  color_t color = body_get_color(body);
  double r = color.red;
  double g = color.green;
  double b = color.blue;

  assert(0 <= r && r <= 1);
  assert(0 <= g && g <= 1);
  assert(0 <= b && b <= 1);

  vector_t window_center = get_window_center();

  // Convert each vertex to a point on screen
  int16_t *x_points = malloc(sizeof(*x_points) * n),
          *y_points = malloc(sizeof(*y_points) * n);
  assert(x_points != NULL);
  assert(y_points != NULL);
  for (size_t i = 0; i < n; i++) {
    vector_t *vertex = list_get(points, i);
    vector_t pixel = get_window_position(*vertex, window_center);
    x_points[i] = pixel.x;
    y_points[i] = pixel.y;
  }

  // Draw body with the given color
  filledPolygonRGBA(renderer, x_points, y_points, n, r * 255, g * 255, b * 255,
                    255);
  sdl_show();
  free(x_points);
  free(y_points);
  list_free(points);
}

SDL_Texture *sdl_get_image_texture(const char *image_path) {
  SDL_Texture *img = IMG_LoadTexture(renderer, image_path);
  return img;
}

SDL_Rect *sdl_get_rect(double x, double y, double w, double h) {
  SDL_Rect *rect = malloc(sizeof(SDL_Rect));
  rect->x = x;
  rect->y = y;
  rect->w = w;
  rect->h = h;
  return rect;
}

void sdl_render_image(SDL_Texture *image_texture, SDL_Rect *rect) {
  SDL_RenderCopy(renderer, image_texture, NULL, rect);
}

void sdl_render_text(const char *text, TTF_Font *font, color_t color,
                     SDL_Rect *rect) {
  SDL_Color sdl_color = (SDL_Color){.r = color.red * 255,
                                    .g = color.green * 255,
                                    .b = color.blue * 255,
                                    .a = 255};
  SDL_Surface *text_surface = TTF_RenderText_Solid(font, text, sdl_color);
  SDL_Texture *text_texture =
      SDL_CreateTextureFromSurface(renderer, text_surface);
  SDL_RenderCopy(renderer, text_texture, NULL, rect);
  SDL_FreeSurface(text_surface);
  SDL_DestroyTexture(text_texture);
}

void sdl_show(void) {
  // Draw boundary lines
  vector_t window_center = get_window_center();
  vector_t max = vec_add(center, max_diff),
           min = vec_subtract(center, max_diff);
  vector_t max_pixel = get_window_position(max, window_center),
           min_pixel = get_window_position(min, window_center);
  SDL_Rect *boundary = malloc(sizeof(*boundary));
  boundary->x = min_pixel.x;
  boundary->y = max_pixel.y;
  boundary->w = max_pixel.x - min_pixel.x;
  boundary->h = min_pixel.y - max_pixel.y;
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderDrawRect(renderer, boundary);
  free(boundary);

  SDL_RenderPresent(renderer);
}

void sdl_render_scene(scene_t *scene) {
  sdl_clear();
  size_t body_count = scene_bodies(scene);
  for (size_t i = 0; i < body_count; i++) {
    body_t *body = scene_get_body(scene, i);
    sdl_draw_body(body);
  }
  sdl_show();
}

void sdl_on_key(key_handler_t handler) { key_handler = handler; }

double time_since_last_tick(void) {
  clock_t now = clock();
  double difference = last_clock
                          ? (double)(now - last_clock) / CLOCKS_PER_SEC
                          : 0.0; // return 0 the first time this is called
  last_clock = now;
  return difference;
}

void sdl_reset_timer(void) { last_clock = clock(); }

void sdl_play_music(const char *path) {
  if (!background_music) {
    background_music = Mix_LoadMUS(path);
    if (!background_music) {
      SDL_Log("Mix_LoadMUS: %s", Mix_GetError());
      return;
    }
  }
  if (Mix_PlayingMusic() == 0) {
    Mix_PlayMusic(background_music, -1);
  }
}

void sdl_play_sound_effect(Mix_Chunk **sound, const char *path) {
  if (!*sound) {
    *sound = Mix_LoadWAV(path);
    if (!*sound) {
      SDL_Log("Mix_LoadWAV: %s", Mix_GetError());
      return;
    }
  }
  Mix_PlayChannel(-1, *sound, 0);
}

void sdl_play_gem_sound(const char *path) {
  sdl_play_sound_effect(&gem_sound, path);
}

void sdl_play_level_completed(const char *path) {
  sdl_play_sound_effect(&level_completed_sound, path);
}

void sdl_play_level_failed(const char *path) {
  sdl_play_sound_effect(&level_failed_sound, path);
}

void sdl_play_jump_sound(const char *path) {
  sdl_play_sound_effect(&jump_sound, path);
}

void sdl_quit() {
  if (background_music) {
    Mix_FreeMusic(background_music);
  }
  if (gem_sound) {
    Mix_FreeChunk(gem_sound);
  }
  if (level_completed_sound) {
    Mix_FreeChunk(level_completed_sound);
  }
  if (level_failed_sound) {
    Mix_FreeChunk(level_failed_sound);
  }
  if (jump_sound) {
    Mix_FreeChunk(jump_sound);
  }
  Mix_CloseAudio();
  SDL_Quit();
}
