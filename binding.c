#include <assert.h>
#include <bare.h>
#include <js.h>

#include "gif.h"

typedef struct {
  const uint8_t *data;
  size_t len;
  size_t offset;
} bare_gif_reader_t;

typedef struct {
  bare_gif_reader_t reader;

  GifFileType *file;

  int duration;
  int timestamp;
  int count;
  int transparent;

  uint64_t pixels;
  int64_t max_pixels;

  GIFDisposeMethod dispose;

  GIFRect rect;

  GIFPicture frame;
  GIFPicture current;
  GIFPicture previous;

  GIFPicture pending;
  int pending_timestamp;
  bool has_pending;

  bool done;
} bare_gif_decoder_t;

static void
bare_gif__on_finalize(js_env_t *env, void *data, void *finalize_hint) {
  free(data);
}

static int
bare_gif__on_read(GifFileType *gif, GifByteType *data, int len) {
  bare_gif_reader_t *reader = gif->UserData;

  if (reader->offset + len <= reader->len) {
    memcpy(data, reader->data + reader->offset, len);

    reader->offset += len;

    return len;
  } else {
    return 0;
  }
}

static inline int
bare_gif__decoder_init(js_env_t *env, bare_gif_decoder_t *decoder, const uint8_t *data, size_t len, int64_t max_pixels) {
  int err;

  decoder->reader = (bare_gif_reader_t){data, len, 0};

  GifFileType *file = DGifOpen(&decoder->reader, bare_gif__on_read, &err);

  if (file == NULL) {
    err = js_throw_error(env, NULL, GIFGetError(file, err));
    assert(err == 0);

    return -1;
  }

  decoder->file = file;
  decoder->duration = 0;
  decoder->timestamp = 0;
  decoder->count = 0;
  decoder->transparent = GIF_INDEX_INVALID;
  decoder->pixels = 0;
  decoder->max_pixels = max_pixels;
  decoder->dispose = GIF_DISPOSE_NONE;
  decoder->has_pending = false;
  decoder->pending_timestamp = 0;
  decoder->done = false;

  err = GIFPictureInit(&decoder->frame);
  assert(err == 1);

  err = GIFPictureInit(&decoder->current);
  assert(err == 1);

  err = GIFPictureInit(&decoder->previous);
  assert(err == 1);

  err = GIFPictureInit(&decoder->pending);
  assert(err == 1);

  return 0;
}

static inline void
bare_gif__decoder_destroy(js_env_t *env, bare_gif_decoder_t *decoder) {
  int err;

  err = DGifCloseFile(decoder->file, &err);
  assert(err == GIF_OK);

  GIFPictureFree(&decoder->frame);
  GIFPictureFree(&decoder->current);
  GIFPictureFree(&decoder->previous);
  GIFPictureFree(&decoder->pending);
}

static inline int
bare_gif__decoder_read_frame(js_env_t *env, bare_gif_decoder_t *decoder, GIFPicture *picture, int *timestamp) {
  int err;

  GifFileType *file = decoder->file;

  if (decoder->done) return 0;

  while (!decoder->done) {
    GifRecordType type;

    if (DGifGetRecordType(file, &type) == GIF_ERROR) goto err;

    switch (type) {
    case IMAGE_DESC_RECORD_TYPE: {
      GifImageDesc *const image = &file->Image;

      err = DGifGetImageDesc(file);
      if (err == GIF_ERROR) goto err;

      if (decoder->count == 0) {
        if (file->SWidth == 0 || file->SHeight == 0) {
          image->Left = 0;
          image->Top = 0;

          file->SWidth = image->Width;
          file->SHeight = image->Height;

          if (file->SWidth <= 0 || file->SHeight <= 0) {
            file->Error = D_GIF_ERR_IMAGE_DEFECT;
            goto err;
          }
        }

        decoder->frame.width = file->SWidth;
        decoder->frame.height = file->SHeight;

        if ((uint64_t) file->SWidth * file->SHeight > GIF_MAX_FRAME_PIXELS) {
          err = js_throw_error(env, NULL, "GIF dimensions exceed maximum");
          assert(err == 0);

          return -1;
        }

        err = GIFPictureAlloc(&decoder->frame);
        if (err != 1) {
          file->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
          goto err;
        }

        GIFPictureClear(&decoder->frame, NULL);

        err = GIFPictureCopy(&decoder->frame, &decoder->current);
        if (err != 1) {
          file->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
          goto err;
        }

        err = GIFPictureCopy(&decoder->frame, &decoder->previous);
        if (err != 1) {
          file->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
          goto err;
        }
      }

      if (image->Width == 0 || image->Height == 0) {
        image->Width = file->SWidth;
        image->Height = file->SHeight;
      }

      err = GIFReadFrame(file, decoder->transparent, &decoder->rect, &decoder->frame);
      if (err != 1) goto err;

      GIFBlendFrames(&decoder->frame, &decoder->rect, &decoder->current);

      if (timestamp) *timestamp = decoder->timestamp;

      decoder->pixels += (uint64_t) decoder->current.width * decoder->current.height;

      if (decoder->max_pixels > 0 && decoder->pixels > (uint64_t) decoder->max_pixels) {
        err = js_throw_error(env, NULL, "GIF exceeds maximum decoded size");
        assert(err == 0);

        return -1;
      }

      err = GIFPictureCopy(&decoder->current, picture);
      if (err != 1) {
        file->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
        goto err;
      }

      decoder->count++;

      GIFDisposeFrame(decoder->dispose, &decoder->rect, &decoder->previous, &decoder->current);

      GIFCopyPixels(&decoder->current, &decoder->previous);

      if (decoder->duration <= 10) decoder->duration = 100;

      decoder->timestamp += decoder->duration;

      decoder->dispose = GIF_DISPOSE_NONE;
      decoder->duration = 0;
      decoder->transparent = GIF_INDEX_INVALID;

      return 1;
    }
    case EXTENSION_RECORD_TYPE: {
      int extension;
      GifByteType *data = NULL;

      err = DGifGetExtension(file, &extension, &data);
      if (err == GIF_ERROR) goto err;

      if (data == NULL) continue;

      if (extension == GRAPHICS_EXT_FUNC_CODE) {
        err = GIFReadGraphicsExtension(data, &decoder->duration, &decoder->dispose, &decoder->transparent);
        if (err != 1) {
          file->Error = D_GIF_ERR_IMAGE_DEFECT;
          goto err;
        }
      }

      while (data) {
        err = DGifGetExtensionNext(file, &data);
        if (err == GIF_ERROR) goto err;
      }

      break;
    }
    case TERMINATE_RECORD_TYPE: {
      decoder->done = true;

      return 0;
    }
    default: {
      break;
    }
    }
  }

err:
  err = js_throw_error(env, NULL, GIFGetError(file, err));
  assert(err == 0);

  return -1;
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

  bare_gif_decoder_t decoder;
  err = bare_gif__decoder_init(env, &decoder, gif, len, 0);
  if (err < 0) return NULL;

  GIFPicture picture;
  err = GIFPictureInit(&picture);
  assert(err == 1);

  err = bare_gif__decoder_read_frame(env, &decoder, &picture, NULL);

  if (err < 0) {
    bare_gif__decoder_destroy(env, &decoder);

    return NULL;
  }

  int width = picture.width;
  int height = picture.height;
  uint32_t *rgba = picture.rgba;

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
  err = js_create_external_arraybuffer(env, rgba, len, bare_gif__on_finalize, NULL, &buffer);
  assert(err == 0);

  err = js_set_named_property(env, result, "data", buffer);
  assert(err == 0);

  bare_gif__decoder_destroy(env, &decoder);

  return result;
}

static void
bare_gif__on_finalize_decoder(js_env_t *env, void *data, void *finalize_hint) {
  bare_gif_decoder_t *decoder = (bare_gif_decoder_t *) data;

  bare_gif__decoder_destroy(env, decoder);

  free(decoder);
}

static js_value_t *
bare_gif_animated_decoder_init(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 2);

  uint8_t *gif;
  size_t len;
  err = js_get_typedarray_info(env, argv[0], NULL, (void **) &gif, &len, NULL, NULL);
  assert(err == 0);

  int64_t max_pixels = 0;
  err = js_get_value_int64(env, argv[1], &max_pixels);
  assert(err == 0);

  bare_gif_decoder_t *decoder = malloc(sizeof(bare_gif_decoder_t));
  assert(decoder != NULL);

  err = bare_gif__decoder_init(env, decoder, gif, len, max_pixels);

  if (err < 0) {
    free(decoder);

    return NULL;
  }

  // The canvas size is only known once a frame has been read, as a screen
  // descriptor of 0x0 adopts the size of the first frame.
  err = bare_gif__decoder_read_frame(env, decoder, &decoder->pending, &decoder->pending_timestamp);

  if (err < 0) {
    bare_gif__decoder_destroy(env, decoder);

    free(decoder);

    return NULL;
  }

  decoder->has_pending = err == 1;

  js_value_t *result;
  err = js_create_external(env, (void *) decoder, bare_gif__on_finalize_decoder, NULL, &result);
  assert(err == 0);

  return result;
}

static js_value_t *
bare_gif_animated_decoder_get_info(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 1;
  js_value_t *argv[1];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 1);

  bare_gif_decoder_t *decoder;
  err = js_get_value_external(env, argv[0], (void **) &decoder);
  assert(err == 0);

  int width = decoder->frame.width;
  int height = decoder->frame.height;

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

  return result;
}

static js_value_t *
bare_gif_animated_decoder_get_next_frame(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 2);

  bare_gif_decoder_t *decoder;
  err = js_get_value_external(env, argv[0], (void **) &decoder);
  assert(err == 0);

  GIFPicture picture;
  int timestamp;

  if (decoder->has_pending) {
    picture = decoder->pending;
    timestamp = decoder->pending_timestamp;

    decoder->has_pending = false;
    decoder->pending.rgba = NULL;
  } else {
    err = GIFPictureInit(&picture);
    assert(err == 1);

    err = bare_gif__decoder_read_frame(env, decoder, &picture, &timestamp);

    if (err < 0) return NULL;

    if (err == 0) {
      js_value_t *result;
      err = js_get_null(env, &result);
      assert(err == 0);

      return result;
    }
  }

  js_value_t *result;
  err = js_create_object(env, &result);
  assert(err == 0);

  js_value_t *value;
  err = js_create_int64(env, timestamp, &value);
  assert(err == 0);

  err = js_set_named_property(env, result, "timestamp", value);
  assert(err == 0);

  js_value_t *buffer;
  err = js_create_external_arraybuffer(env, picture.rgba, (size_t) picture.width * picture.height * 4, bare_gif__on_finalize, NULL, &buffer);
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
  V("animatedDecoderInit", bare_gif_animated_decoder_init)
  V("animatedDecoderGetInfo", bare_gif_animated_decoder_get_info)
  V("animatedDecoderGetNextFrame", bare_gif_animated_decoder_get_next_frame)
#undef V

  js_value_t *max_pixels;
  err = js_create_int64(env, GIF_DEFAULT_MAX_PIXELS, &max_pixels);
  assert(err == 0);

  err = js_set_named_property(env, exports, "defaultMaxPixels", max_pixels);
  assert(err == 0);

  return exports;
}

BARE_MODULE(bare_gif, bare_gif_exports)
