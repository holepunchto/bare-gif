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
