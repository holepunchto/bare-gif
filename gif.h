// Copyright (c) 2010, Google Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//   * Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in
//     the documentation and/or other materials provided with the
//     distribution.
//
//   * Neither the name of Google nor the names of its contributors may
//     be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <assert.h>
#include <gif_lib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIF_INDEX_INVALID     -1
#define GIF_TRANSPARENT_COLOR 0x00000000u
#define GIF_WHITE_COLOR       0xffffffffu
#define GIF_TRANSPARENT_MASK  0x01
#define GIF_DISPOSE_MASK      0x07
#define GIF_DISPOSE_SHIFT     2

// Cap total picture size to keep attacker-controlled dimensions from reaching
// the allocator. 1 GiB at 4 bytes per pixel covers up to a 16384x16384 canvas.
#define GIF_MAX_PIXELS (1ULL << 28)

// Cap a single frame whatever budget the caller asks for. Every frame is canvas
// sized and the canvas is allocated three times over as the working state, so
// 64 Mpx at 4 bytes per pixel bounds that at 768 MiB.
#define GIF_MAX_FRAME_PIXELS (1ULL << 26)

// Budget for the pixels one decode may produce in total, for callers that
// retain what they are handed. A frame count is cheap to inflate in the file,
// so the per-frame cap alone leaves the total unbounded.
#define GIF_DEFAULT_MAX_PIXELS (1ULL << 26)

// A GIF pixel is an index into a colour map of at most 256 entries.
#define GIF_MAX_COLORS 256

// Colours are bucketed to 5 bits per channel before the palette is cut. 32768
// buckets keep the histogram at half a megabyte and the error below what the
// 256 entry palette costs anyway.
#define GIF_QUANTIZE_BITS 5
#define GIF_QUANTIZE_SIZE (1 << (3 * GIF_QUANTIZE_BITS))

// A GIF has no alpha channel, only a fully transparent palette index.
#define GIF_ALPHA_THRESHOLD 128

// Hundredths of a second a frame is shown for when nothing says otherwise.
#define GIF_DEFAULT_DELAY 10

#define GIF_BUFFER_MIN_CAPACITY 4096

// An encode writes at most a byte per pixel plus the LZW and block overhead, so
// this only ever trips on a caller that ignores GIF_MAX_PIXELS.
#define GIF_MAX_OUTPUT (1ULL << 30)

typedef enum GIFDisposeMethod {
  GIF_DISPOSE_NONE,
  GIF_DISPOSE_BACKGROUND,
  GIF_DISPOSE_RESTORE_PREVIOUS
} GIFDisposeMethod;

typedef struct {
  const uint8_t *data;
  size_t len;
} GIFData;

typedef struct {
  int width, height;
  uint32_t *rgba;
  int stride;
} GIFPicture;

typedef struct {
  int x, y, width, height;
} GIFRect;

typedef struct {
  uint8_t red, green, blue, alpha;
} GIFPixel;

static void
GIFCopyPlane(const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride, int width, int height) {
  assert(src != NULL && dst != NULL);
  assert(abs(src_stride) >= width && abs(dst_stride) >= width);

  while (height-- > 0) {
    memcpy(dst, src, width);

    src += src_stride;
    dst += dst_stride;
  }
}

static void
GIFCopyPixels(const GIFPicture *const src, GIFPicture *const dst) {
  assert(src != NULL && dst != NULL);
  assert(src->width == dst->width && src->height == dst->height);

  GIFCopyPlane((uint8_t *) src->rgba, 4 * src->stride, (uint8_t *) dst->rgba, 4 * dst->stride, 4 * src->width, src->height);
}

static int
GIFPictureInit(GIFPicture *const picture) {
  memset(picture, 0, sizeof(*picture));

  return 1;
}

static void
GIFPictureFree(GIFPicture *picture);

static int
GIFPictureAlloc(GIFPicture *const picture) {
  GIFPictureFree(picture);

  void *argb;
  const int width = picture->width;
  const int height = picture->height;

  if (width <= 0 || height <= 0) return 0;

  const uint64_t size = (uint64_t) width * height;

  if (size > GIF_MAX_PIXELS) return 0;

  argb = malloc(size * sizeof(*picture->rgba));

  if (argb == NULL) return 0;

  picture->rgba = argb;
  picture->stride = width;

  return 1;
}

static void
GIFPictureFree(GIFPicture *picture) {
  free(picture->rgba);

  picture->rgba = NULL;
  picture->stride = 0;
}

static int
GIFPictureView(const GIFPicture *src, int x, int y, int width, int height, GIFPicture *dst) {
  if (src == NULL || dst == NULL) return 0;

  if (x < 0 || y < 0 || width <= 0 || height <= 0) return 0;
  if (x > src->width - width || y > src->height - height) return 0;

  if (src != dst) *dst = *src;

  dst->width = width;
  dst->height = height;

  dst->rgba = src->rgba + y * src->stride + x;
  dst->stride = src->stride;

  return 1;
}

static int
GIFPictureCopy(const GIFPicture *src, GIFPicture *dst) {
  int err;

  if (src == NULL || dst == NULL) return 0;
  if (src == dst) return 1;

  *dst = *src;

  dst->rgba = NULL;

  err = GIFPictureAlloc(dst);
  if (err != 1) return err;

  GIFCopyPlane((const uint8_t *) src->rgba, 4 * src->stride, (uint8_t *) dst->rgba, 4 * dst->stride, 4 * dst->width, dst->height);

  return 1;
}

static void
GIFPictureClearRectangle(GIFPicture *const picture, int x, int y, int width, int height) {
  const size_t stride = picture->stride;

  uint32_t *dst = picture->rgba + y * stride + x;

  for (int j = 0; j < height; j++, dst += stride) {
    for (int i = 0; i < width; i++) {
      dst[i] = GIF_TRANSPARENT_COLOR;
    }
  }
}

static void
GIFPictureClear(GIFPicture *const picture, const GIFRect *const rect) {
  if (rect) {
    GIFPictureClearRectangle(picture, rect->x, rect->y, rect->width, rect->height);
  } else {
    GIFPictureClearRectangle(picture, 0, 0, picture->width, picture->height);
  }
}

static int
GIFReadGraphicsExtension(const GifByteType *const data, int *const duration, GIFDisposeMethod *const dispose, int *const transparent) {
  if (data[0] != 4) return 0;

  const int flags = data[1];

  *duration = (data[2] | (data[3] << 8)) /* 10 ms units */ * 10;

  switch ((flags >> GIF_DISPOSE_SHIFT) & GIF_DISPOSE_MASK) {
  case 3:
    *dispose = GIF_DISPOSE_RESTORE_PREVIOUS;
    break;
  case 2:
    *dispose = GIF_DISPOSE_BACKGROUND;
    break;
  case 1:
  case 0:
  default:
    *dispose = GIF_DISPOSE_NONE;
    break;
  }

  *transparent = (flags & GIF_TRANSPARENT_MASK) ? data[4] : GIF_INDEX_INVALID;

  return 1;
}

static int
GIFRemap(const GifFileType *const gif, const uint8_t *const src, int len, int transparent, uint32_t *dst) {
  const ColorMapObject *const map = gif->Image.ColorMap ? gif->Image.ColorMap : gif->SColorMap;

  if (map == NULL) return 1;
  if (map->Colors == NULL || map->ColorCount <= 0) return 0;

  const GifColorType *colors = map->Colors;

  for (int i = 0; i < len; i++) {
    if (src[i] == transparent) {
      dst[i] = GIF_TRANSPARENT_COLOR;
    } else if (src[i] < map->ColorCount) {
      const GifColorType c = colors[src[i]];

      GIFPixel *pixel = (GIFPixel *) &dst[i];

      pixel->red = c.Red;
      pixel->green = c.Green;
      pixel->blue = c.Blue;
      pixel->alpha = 0xff;
    } else {
      return 0;
    }
  }

  return 1;
}

static int
GIFReadFrame(GifFileType *const gif, int transparent, GIFRect *const rectp, GIFPicture *const picture) {
  int err;

  GIFPicture view;
  uint8_t *tmp = NULL;

  const GifImageDesc *const image = &gif->Image;

  const GIFRect rect = {image->Left, image->Top, image->Width, image->Height};

  *rectp = rect;

  if (rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 || rect.x > picture->width - rect.width || rect.y > picture->height - rect.height) {
    gif->Error = D_GIF_ERR_IMAGE_DEFECT;
    goto err;
  }

  err = GIFPictureView(picture, rect.x, rect.y, rect.width, rect.height, &view);
  if (err != 1) {
    gif->Error = D_GIF_ERR_IMAGE_DEFECT;
    goto err;
  }

  uint32_t *dst = view.rgba;

  tmp = malloc(rect.width * sizeof(*tmp));

  if (tmp == NULL) {
    gif->Error = D_GIF_ERR_NOT_ENOUGH_MEM;
    goto err;
  }

  if (image->Interlace) {
    const int offsets[] = {0, 4, 2, 1};
    const int jumps[] = {8, 8, 4, 2};

    for (int pass = 0; pass < 4; pass++) {
      const size_t stride = (size_t) view.stride;

      int y = offsets[pass];

      uint32_t *row = dst + y * stride;

      const size_t jump = jumps[pass] * stride;

      for (; y < rect.height; y += jumps[pass], row += jump) {
        err = DGifGetLine(gif, tmp, rect.width);
        if (err == GIF_ERROR) goto err;

        err = GIFRemap(gif, tmp, rect.width, transparent, row);
        if (err != 1) {
          gif->Error = D_GIF_ERR_IMAGE_DEFECT;
          goto err;
        }
      }
    }
  } else {
    uint32_t *ptr = dst;

    for (int y = 0; y < rect.height; y++, ptr += view.stride) {
      err = DGifGetLine(gif, tmp, rect.width);
      if (err == GIF_ERROR) goto err;

      err = GIFRemap(gif, tmp, rect.width, transparent, ptr);
      if (err != 1) {
        gif->Error = D_GIF_ERR_IMAGE_DEFECT;
        goto err;
      }
    }
  }

  free(tmp);
  return 1;

err:
  free(tmp);
  return 0;
}

static void
GIFDisposeFrame(GIFDisposeMethod dispose, const GIFRect *const rect, const GIFPicture *const previous, GIFPicture *const current) {
  assert(rect);

  if (dispose == GIF_DISPOSE_BACKGROUND) {
    GIFPictureClear(current, rect);
  } else if (dispose == GIF_DISPOSE_RESTORE_PREVIOUS) {
    const size_t src_stride = previous->stride;
    const uint32_t *const src = previous->rgba + rect->x + rect->y * src_stride;

    const size_t dst_stride = current->stride;
    uint32_t *const dst = current->rgba + rect->x + rect->y * dst_stride;

    assert(previous);

    GIFCopyPlane((uint8_t *) src, (int) (4 * src_stride), (uint8_t *) dst, (int) (4 * dst_stride), 4 * rect->width, rect->height);
  }
}

static void
GIFBlendFrames(const GIFPicture *const src, const GIFRect *const rect, GIFPicture *const dst) {
  const size_t src_stride = src->stride;
  const size_t dst_stride = dst->stride;

  assert(src->width == dst->width && src->height == dst->height);

  if (rect->width <= 0 || rect->height <= 0 || rect->x < 0 || rect->y < 0 || rect->x > src->width - rect->width || rect->y > src->height - rect->height) {
    return;
  }

  for (int j = rect->y; j < rect->y + rect->height; j++) {
    for (int i = rect->x; i < rect->x + rect->width; i++) {
      const GIFPixel *pixel = (GIFPixel *) &src->rgba[j * src_stride + i];

      if (pixel->alpha != 0) {
        memcpy(&dst->rgba[j * dst_stride + i], pixel, sizeof(*pixel));
      }
    }
  }
}

static const char *
GIFGetError(const GifFileType *const gif, int error) {
  const char *message = GifErrorString(gif == NULL ? error : gif->Error);
  if (message == NULL) message = "Unknown error";
  return message;
}

typedef struct {
  uint8_t *data;
  size_t len;
  size_t capacity;
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
      if (capacity > GIF_MAX_OUTPUT / 2) return 0;

      capacity *= 2;
    }

    uint8_t *resized = realloc(buffer->data, capacity);

    if (resized == NULL) return 0;

    buffer->data = resized;
    buffer->capacity = capacity;
  }

  memcpy(buffer->data + buffer->len, data, len);

  buffer->len += len;

  return 1;
}

typedef struct {
  uint32_t count;
  uint32_t red, green, blue;
} GIFColorCell;

typedef struct {
  int begin, end;
} GIFColorBox;

typedef struct {
  GIFColorCell *cells;

  uint16_t *keys;
  uint16_t *scratch;
  int key_count;

  GifColorType colors[GIF_MAX_COLORS];
  int color_count;

  // A GIF pixel is either opaque or fully transparent, so the alpha channel
  // collapses onto a single reserved palette index.
  bool transparency;
  int transparent;

  uint8_t lookup[GIF_QUANTIZE_SIZE];
} GIFPalette;

static inline int
GIFQuantizeKey(const GIFPixel *const pixel) {
  return ((pixel->red >> (8 - GIF_QUANTIZE_BITS)) << (2 * GIF_QUANTIZE_BITS)) | ((pixel->green >> (8 - GIF_QUANTIZE_BITS)) << GIF_QUANTIZE_BITS) | (pixel->blue >> (8 - GIF_QUANTIZE_BITS));
}

static void
GIFPaletteFree(GIFPalette *const palette);

static int
GIFPaletteInit(GIFPalette *const palette) {
  memset(palette, 0, sizeof(*palette));

  palette->transparent = GIF_INDEX_INVALID;

  palette->cells = calloc(GIF_QUANTIZE_SIZE, sizeof(*palette->cells));
  palette->keys = malloc(GIF_QUANTIZE_SIZE * sizeof(*palette->keys));
  palette->scratch = malloc(GIF_QUANTIZE_SIZE * sizeof(*palette->scratch));

  if (palette->cells == NULL || palette->keys == NULL || palette->scratch == NULL) {
    GIFPaletteFree(palette);

    return 0;
  }

  return 1;
}

static void
GIFPaletteFree(GIFPalette *const palette) {
  free(palette->cells);
  free(palette->keys);
  free(palette->scratch);

  palette->cells = NULL;
  palette->keys = NULL;
  palette->scratch = NULL;
}

static void
GIFPaletteSample(GIFPalette *const palette, const GIFPicture *const picture) {
  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      if (pixel->alpha < GIF_ALPHA_THRESHOLD) {
        palette->transparency = true;

        continue;
      }

      GIFColorCell *const cell = &palette->cells[GIFQuantizeKey(pixel)];

      cell->count++;
      cell->red += pixel->red;
      cell->green += pixel->green;
      cell->blue += pixel->blue;
    }
  }
}

static int
GIFColorBoxWidest(const GIFPalette *const palette, const GIFColorBox *const box, int *const shift) {
  const int mask = (1 << GIF_QUANTIZE_BITS) - 1;

  int widest = 0;

  for (int channel = 0; channel < 3; channel++) {
    const int bits = (2 - channel) * GIF_QUANTIZE_BITS;

    int low = mask;
    int high = 0;

    for (int i = box->begin; i < box->end; i++) {
      const int value = (palette->keys[i] >> bits) & mask;

      if (value < low) low = value;
      if (value > high) high = value;
    }

    if (high - low > widest) {
      widest = high - low;

      *shift = bits;
    }
  }

  return widest;
}

static void
GIFColorBoxSort(GIFPalette *const palette, const GIFColorBox *const box, int shift) {
  const int mask = (1 << GIF_QUANTIZE_BITS) - 1;

  int offsets[1 << GIF_QUANTIZE_BITS] = {0};

  for (int i = box->begin; i < box->end; i++) {
    offsets[(palette->keys[i] >> shift) & mask]++;
  }

  int offset = box->begin;

  for (int value = 0; value <= mask; value++) {
    const int count = offsets[value];

    offsets[value] = offset;

    offset += count;
  }

  memcpy(palette->scratch + box->begin, palette->keys + box->begin, (size_t) (box->end - box->begin) * sizeof(*palette->keys));

  for (int i = box->begin; i < box->end; i++) {
    const uint16_t key = palette->scratch[i];

    palette->keys[offsets[(key >> shift) & mask]++] = key;
  }
}

// Split where the two halves carry the same number of pixels, so that colours
// get spent on the areas of the image that use them.
static int
GIFColorBoxSplit(const GIFPalette *const palette, const GIFColorBox *const box) {
  uint64_t total = 0;

  for (int i = box->begin; i < box->end; i++) {
    total += palette->cells[palette->keys[i]].count;
  }

  uint64_t seen = 0;

  for (int i = box->begin; i < box->end - 1; i++) {
    seen += palette->cells[palette->keys[i]].count;

    if (seen * 2 >= total) return i + 1;
  }

  return box->end - 1;
}

static void
GIFPaletteResolve(GIFPalette *const palette) {
  for (int i = 0; i < palette->key_count; i++) {
    const int key = palette->keys[i];

    const GIFColorCell *const cell = &palette->cells[key];

    const int red = (int) (cell->red / cell->count);
    const int green = (int) (cell->green / cell->count);
    const int blue = (int) (cell->blue / cell->count);

    int64_t nearest = -1;
    int best = 0;

    for (int j = 0; j < palette->color_count; j++) {
      const int dr = red - palette->colors[j].Red;
      const int dg = green - palette->colors[j].Green;
      const int db = blue - palette->colors[j].Blue;

      const int64_t distance = (int64_t) dr * dr + (int64_t) dg * dg + (int64_t) db * db;

      if (nearest < 0 || distance < nearest) {
        nearest = distance;
        best = j;
      }
    }

    palette->lookup[key] = (uint8_t) best;
  }
}

// Median cut: start with every sampled colour in one box and keep splitting the
// box that spans the widest channel until the palette is full.
static void
GIFPaletteBuild(GIFPalette *const palette, int max_colors) {
  palette->key_count = 0;

  for (int key = 0; key < GIF_QUANTIZE_SIZE; key++) {
    if (palette->cells[key].count > 0) palette->keys[palette->key_count++] = (uint16_t) key;
  }

  if (palette->transparency) max_colors--;
  if (max_colors < 1) max_colors = 1;

  GIFColorBox boxes[GIF_MAX_COLORS];

  int count = 0;

  if (palette->key_count > 0) {
    boxes[count++] = (GIFColorBox) {0, palette->key_count};
  }

  while (count > 0 && count < max_colors) {
    int target = GIF_INDEX_INVALID;
    int widest = 0;
    int shift = 0;

    for (int i = 0; i < count; i++) {
      int candidate = 0;

      const int range = GIFColorBoxWidest(palette, &boxes[i], &candidate);

      if (range > widest) {
        widest = range;
        shift = candidate;
        target = i;
      }
    }

    if (target == GIF_INDEX_INVALID) break;

    GIFColorBoxSort(palette, &boxes[target], shift);

    const int middle = GIFColorBoxSplit(palette, &boxes[target]);

    boxes[count++] = (GIFColorBox) {middle, boxes[target].end};

    boxes[target].end = middle;
  }

  for (int i = 0; i < count; i++) {
    uint64_t total = 0, red = 0, green = 0, blue = 0;

    for (int j = boxes[i].begin; j < boxes[i].end; j++) {
      const GIFColorCell *const cell = &palette->cells[palette->keys[j]];

      total += cell->count;
      red += cell->red;
      green += cell->green;
      blue += cell->blue;
    }

    if (total == 0) total = 1;

    palette->colors[i].Red = (GifByteType) (red / total);
    palette->colors[i].Green = (GifByteType) (green / total);
    palette->colors[i].Blue = (GifByteType) (blue / total);
  }

  palette->color_count = count;
  palette->transparent = palette->transparency ? count : GIF_INDEX_INVALID;

  GIFPaletteResolve(palette);
}

static void
GIFPaletteMap(const GIFPalette *const palette, const GIFPicture *const picture, uint8_t *const indices) {
  for (int y = 0; y < picture->height; y++) {
    const uint32_t *const row = picture->rgba + (size_t) y * picture->stride;

    uint8_t *const dst = indices + (size_t) y * picture->width;

    for (int x = 0; x < picture->width; x++) {
      const GIFPixel *const pixel = (const GIFPixel *) &row[x];

      dst[x] = pixel->alpha < GIF_ALPHA_THRESHOLD ? (uint8_t) palette->transparent : palette->lookup[GIFQuantizeKey(pixel)];
    }
  }
}

// A colour map has a power of two entries, and the reserved transparent index
// sits past the colours the quantizer produced.
static ColorMapObject *
GIFPaletteToMap(const GIFPalette *const palette) {
  int count = palette->color_count + (palette->transparency ? 1 : 0);

  if (count < 1) count = 1;

  ColorMapObject *const map = GifMakeMapObject(1 << GifBitSize(count), NULL);

  if (map == NULL) return NULL;

  for (int i = 0; i < palette->color_count; i++) {
    map->Colors[i] = palette->colors[i];
  }

  return map;
}

static int
GIFWriteLoop(GifFileType *const gif, int loops) {
  const GifByteType block[] = {1, loops & 0xff, (loops >> 8) & 0xff};

  if (EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_ERROR) return 0;
  if (EGifPutExtensionBlock(gif, 11, (const GifByteType *) "NETSCAPE2.0") == GIF_ERROR) return 0;
  if (EGifPutExtensionBlock(gif, sizeof(block), block) == GIF_ERROR) return 0;
  if (EGifPutExtensionTrailer(gif) == GIF_ERROR) return 0;

  return 1;
}

static int
GIFWriteFrame(GifFileType *const gif, const uint8_t *const indices, int width, int height, int delay, int transparent, ColorMapObject *const map) {
  if (delay > 0 || transparent != GIF_INDEX_INVALID) {
    const GraphicsControlBlock gcb = {
      // Every frame is written as a full canvas, so clearing between frames is
      // what keeps a transparent pixel transparent.
      .DisposalMode = transparent == GIF_INDEX_INVALID ? DISPOSAL_UNSPECIFIED : DISPOSE_BACKGROUND,
      .UserInputFlag = false,
      .DelayTime = delay,
      .TransparentColor = transparent
    };

    GifByteType extension[4];

    const size_t len = EGifGCBToExtension(&gcb, extension);

    if (EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, (int) len, extension) == GIF_ERROR) return 0;
  }

  if (EGifPutImageDesc(gif, 0, 0, width, height, false, map) == GIF_ERROR) return 0;

  for (int y = 0; y < height; y++) {
    if (EGifPutLine(gif, (GifPixelType *) indices + (size_t) y * width, width) == GIF_ERROR) return 0;
  }

  return 1;
}
