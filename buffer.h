#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GIF_BUFFER_MIN_CAPACITY 4096

// An encode writes at most a byte per pixel plus the LZW and block overhead, so
// this only ever trips on a caller that ignores GIF_MAX_PIXELS.
#define GIF_MAX_OUTPUT (1ULL << 30)

typedef struct {
  uint8_t *data;
  size_t len;
  size_t capacity;

  // giflib drops the result of its write callback on most paths, so a failure
  // has to survive until someone asks for the bytes.
  bool failed;
} GIFBuffer;

static void
GIFBufferInit(GIFBuffer *const buffer) {
  memset(buffer, 0, sizeof(*buffer));
}

static void
GIFBufferFree(GIFBuffer *const buffer) {
  free(buffer->data);

  GIFBufferInit(buffer);
}

static int
GIFBufferWrite(GIFBuffer *const buffer, const uint8_t *const data, size_t len) {
  if (buffer->len + len > buffer->capacity) {
    size_t capacity = buffer->capacity == 0 ? GIF_BUFFER_MIN_CAPACITY : buffer->capacity;

    while (capacity < buffer->len + len) {
      if (capacity > GIF_MAX_OUTPUT / 2) {
        buffer->failed = true;

        return 0;
      }

      capacity *= 2;
    }

    uint8_t *resized = realloc(buffer->data, capacity);

    if (resized == NULL) {
      buffer->failed = true;

      return 0;
    }

    buffer->data = resized;
    buffer->capacity = capacity;
  }

  memcpy(buffer->data + buffer->len, data, len);

  buffer->len += len;

  return 1;
}
