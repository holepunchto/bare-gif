#pragma once

#include <assert.h>
#include <gif_lib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gif.h"

#define GIF_COLOR_CHANNELS 3

#define GIF_MIN_COLORS 2
#define GIF_MAX_COLORS 256

#define GIF_ALPHA_THRESHOLD 128

int
GifQuantizeBuffer(
  unsigned int width,
  unsigned int height,
  int *color_map_size,
  const GifByteType *red,
  const GifByteType *green,
  const GifByteType *blue,
  GifByteType *output,
  GifColorType *color_map
);

typedef struct {
  GifByteType *indices;
  ColorMapObject *map;
  int transparent;
} GIFQuantized;

static void
GIFQuantizedFree(GIFQuantized *const quantized) {
  free(quantized->indices);

  if (quantized->map) GifFreeMapObject(quantized->map);

  quantized->indices = NULL;
  quantized->map = NULL;
}

static void
GIFRefinePalette(
  const GIFPicture *const picture,
  const GifByteType *const indices,
  int size,
  GifColorType *const palette
) {
  assert(size >= 0 && size <= GIF_MAX_COLORS);

  uint64_t sums[GIF_MAX_COLORS][3] = {{0}};
  uint64_t counts[GIF_MAX_COLORS] = {0};

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) continue;

      const size_t offset = (size_t) y * picture->width + x;

      const int index = indices[offset];

      sums[index][0] += pixel->red;
      sums[index][1] += pixel->green;
      sums[index][2] += pixel->blue;

      counts[index]++;
    }
  }

  for (int i = 0; i < size; i++) {
    if (counts[i] == 0) continue;

    palette[i].Red = (GifByteType) (sums[i][0] / counts[i]);
    palette[i].Green = (GifByteType) (sums[i][1] / counts[i]);
    palette[i].Blue = (GifByteType) (sums[i][2] / counts[i]);
  }
}

#define GIF_COLOR_SLOT_BITS 9
#define GIF_COLOR_SLOTS     (1 << GIF_COLOR_SLOT_BITS)

#define GIF_COLOR_EMPTY 0xffffffffu

#define GIF_COLOR_HASH 2654435761u

typedef struct {
  uint32_t keys[GIF_COLOR_SLOTS];
  uint8_t values[GIF_COLOR_SLOTS];
  int count;
} GIFColorSet;

static inline uint32_t
GIFColorKey(const GIFPixel *const pixel) {
  return ((uint32_t) pixel->red << 16) | ((uint32_t) pixel->green << 8) | pixel->blue;
}

static inline int
GIFColorSetSlot(const GIFColorSet *const set, uint32_t key) {
  int slot = (int) ((key * GIF_COLOR_HASH) >> (32 - GIF_COLOR_SLOT_BITS));

  while (set->keys[slot] != GIF_COLOR_EMPTY && set->keys[slot] != key) {
    slot = (slot + 1) & (GIF_COLOR_SLOTS - 1);
  }

  return slot;
}

static bool
GIFHasTransparency(const GIFPicture *const picture) {
  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      if (((const GIFPixel *) &row[x])->alpha < GIF_ALPHA_THRESHOLD) return true;
    }
  }

  return false;
}

static int
GIFColorSetBuild(GIFColorSet *const set, const GIFPicture *const picture, int colors) {
  memset(set->keys, 0xff, sizeof(set->keys));

  set->count = 0;

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) continue;

      const int slot = GIFColorSetSlot(set, GIFColorKey(pixel));

      if (set->keys[slot] != GIF_COLOR_EMPTY) continue;

      if (set->count == colors) return 0;

      set->keys[slot] = GIFColorKey(pixel);
      set->values[slot] = (uint8_t) set->count++;
    }
  }

  return 1;
}

static void
GIFColorSetResolve(
  const GIFColorSet *const set,
  const GIFPicture *const picture,
  GifColorType *const palette,
  GifByteType *const indices
) {
  for (int slot = 0; slot < GIF_COLOR_SLOTS; slot++) {
    const uint32_t key = set->keys[slot];

    if (key == GIF_COLOR_EMPTY) continue;

    GifColorType *const colour = &palette[set->values[slot]];

    colour->Red = (GifByteType) (key >> 16);
    colour->Green = (GifByteType) (key >> 8);
    colour->Blue = (GifByteType) key;
  }

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      const size_t offset = (size_t) y * picture->width + x;

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) {
        indices[offset] = 0;

        continue;
      }

      indices[offset] = set->values[GIFColorSetSlot(set, GIFColorKey(pixel))];
    }
  }
}

static int
GIFQuantizeBuckets(
  const GIFPicture *const picture,
  int *const size,
  GifColorType *const palette,
  GifByteType *const indices
) {
  const size_t pixels = (size_t) picture->width * (size_t) picture->height;

  GifByteType *planes = malloc(GIF_COLOR_CHANNELS * pixels);
  GifByteType *opaque = malloc(pixels);

  if (planes == NULL || opaque == NULL) {
    free(planes);
    free(opaque);

    return 0;
  }

  GifByteType *const red = planes;
  GifByteType *const green = red + pixels;
  GifByteType *const blue = green + pixels;

  size_t count = 0;

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) continue;

      red[count] = pixel->red;
      green[count] = pixel->green;
      blue[count] = pixel->blue;

      count++;
    }
  }

  const int err = GifQuantizeBuffer(
    (unsigned int) count,
    1,
    size,
    red,
    green,
    blue,
    opaque,
    palette
  );

  free(planes);

  if (err == GIF_ERROR) {
    free(opaque);

    return 0;
  }

  size_t i = 0;

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      const size_t offset = (size_t) y * picture->width + x;

      indices[offset] = pixel->alpha < GIF_ALPHA_THRESHOLD ? 0 : opaque[i++];
    }
  }

  free(opaque);

  GIFRefinePalette(picture, indices, *size, palette);

  return 1;
}

static int
GIFQuantize(const GIFPicture *const picture, int colors, GIFQuantized *const quantized) {
  const size_t pixels = (size_t) picture->width * (size_t) picture->height;

  memset(quantized, 0, sizeof(*quantized));

  quantized->transparent = GIF_INDEX_INVALID;

  GifByteType *indices = malloc(pixels);

  if (indices == NULL) return 0;

  const bool transparency = GIFHasTransparency(picture);

  assert(colors >= GIF_MIN_COLORS && colors <= GIF_MAX_COLORS);

  int size = transparency ? colors - 1 : colors;

  if (size < 1) size = 1;

  GifColorType palette[GIF_MAX_COLORS];

  GIFColorSet set;

  if (GIFColorSetBuild(&set, picture, size) == 1) {
    GIFColorSetResolve(&set, picture, palette, indices);

    size = set.count;
  } else if (GIFQuantizeBuckets(picture, &size, palette, indices) != 1) {
    free(indices);

    return 0;
  }

  if (transparency) {
    for (int y = 0; y < picture->height; y++) {
      const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

      for (int x = 0; x < picture->width; x++) {
        const GIFPixel *const pixel = (const GIFPixel *) &row[x];

        const size_t offset = (size_t) y * picture->width + x;

        if (pixel->alpha < GIF_ALPHA_THRESHOLD) indices[offset] = (GifByteType) size;
      }
    }

    quantized->transparent = size;
  }

  quantized->map = GifMakeMapObject(1 << GifBitSize(size + (transparency ? 1 : 0)), NULL);

  if (quantized->map == NULL) {
    free(indices);

    return 0;
  }

  memcpy(quantized->map->Colors, palette, (size_t) size * sizeof(*palette));

  quantized->indices = indices;

  return 1;
}
