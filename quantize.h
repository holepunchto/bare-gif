#pragma once

#include <gif_lib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gif.h"

// A GIF pixel is an index into a colour map of at most 256 entries.
#define GIF_MAX_COLORS 256

// A GIF has no alpha channel, only a fully transparent palette index.
#define GIF_ALPHA_THRESHOLD 128

// giflib still ships and maintains its quantizer, it just keeps it out of the
// public header.
int
GifQuantizeBuffer(unsigned int width, unsigned int height, int *color_map_size, const GifByteType *red, const GifByteType *green, const GifByteType *blue, GifByteType *output, GifColorType *color_map);

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

// giflib averages the 5 bit buckets it sorts on, so every channel lands on a
// multiple of 8 and pure white comes back as 0xf8. The boxes it picked are
// right, only the colour it names them is rounded, so average the real pixels.
static void
GIFRefinePalette(const GIFPicture *const picture, const GifByteType *const indices, int size, GifColorType *const palette) {
  uint64_t sums[GIF_MAX_COLORS][3] = {{0}};
  uint64_t counts[GIF_MAX_COLORS] = {0};

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) continue;

      const int index = indices[(size_t) y * picture->width + x];

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

static int
GIFQuantize(const GIFPicture *const picture, int colors, GIFQuantized *const quantized) {
  const size_t pixels = (size_t) picture->width * (size_t) picture->height;

  memset(quantized, 0, sizeof(*quantized));

  quantized->transparent = GIF_INDEX_INVALID;

  // The quantizer reads one plane per channel, the picture holds them
  // interleaved.
  GifByteType *planes = malloc(3 * pixels);
  GifByteType *indices = malloc(pixels);

  if (planes == NULL || indices == NULL) {
    free(planes);
    free(indices);

    return 0;
  }

  GifByteType *const red = planes;
  GifByteType *const green = planes + pixels;
  GifByteType *const blue = planes + 2 * pixels;

  bool transparency = false;

  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      const size_t i = (size_t) y * picture->width + x;

      red[i] = pixel->red;
      green[i] = pixel->green;
      blue[i] = pixel->blue;

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) transparency = true;
    }
  }

  // A GIF has no alpha channel, only an index that reads as transparent, so
  // transparency costs one of the colours.
  int size = transparency ? colors - 1 : colors;

  if (size < 1) size = 1;

  GifColorType palette[GIF_MAX_COLORS];

  const int err = GifQuantizeBuffer((unsigned int) picture->width, (unsigned int) picture->height, &size, red, green, blue, indices, palette);

  free(planes);

  if (err == GIF_ERROR) {
    free(indices);

    return 0;
  }

  GIFRefinePalette(picture, indices, size, palette);

  if (transparency) {
    for (int y = 0; y < picture->height; y++) {
      const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

      for (int x = 0; x < picture->width; x++) {
        const GIFPixel *const pixel = (const GIFPixel *) &row[x];

        if (pixel->alpha < GIF_ALPHA_THRESHOLD) indices[(size_t) y * picture->width + x] = (GifByteType) size;
      }
    }

    quantized->transparent = size;
  }

  // A colour map holds a power of two entries, and the transparent index sits
  // past the colours the quantizer settled on.
  quantized->map = GifMakeMapObject(1 << GifBitSize(size + (transparency ? 1 : 0)), NULL);

  if (quantized->map == NULL) {
    free(indices);

    return 0;
  }

  memcpy(quantized->map->Colors, palette, (size_t) size * sizeof(*palette));

  quantized->indices = indices;

  return 1;
}
