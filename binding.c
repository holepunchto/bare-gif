#include <assert.h>
#include <bare.h>
#include <js.h>

#define STBI_ONLY_GIF
#define STBI_NO_STDIO

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC

#include <stb_image.h>

static void
bare_gif__on_finalize(js_env_t *env, void *data, void *finalize_hint) {
  free(data);
}

static js_value_t *
bare_gif_decode(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 1);

  uint8_t *gif;
  size_t len;
  err = js_get_typedarray_info(env, argv[0], NULL, (void **) &gif, &len, NULL, NULL);
  assert(err == 0);

  int width, height, channels_in_file;

  uint8_t *data = stbi_load_from_memory(gif, len, &width, &height, &channels_in_file, 4);

  js_value_t *result;
  err = js_create_object(env, &result);
  assert(err == 0);

#define V(n) \
  { \
    js_value_t *val; \
    err = js_create_int64(env, n, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, result, #n, val); \
    assert(err == 0); \
  }

  V(width);
  V(height);
#undef V

  len = width * height * 4;

  js_value_t *buffer;
  err = js_create_external_arraybuffer(env, data, len, bare_gif__on_finalize, NULL, &buffer);
  assert(err == 0);

  err = js_set_named_property(env, result, "data", buffer);
  assert(err == 0);

  return result;
}

static js_value_t *
bare_gif_exports(js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("decode", bare_gif_decode)
#undef V

  return exports;
}

BARE_MODULE(bare_gif, bare_gif_exports)
