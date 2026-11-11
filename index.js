const binding = require('./binding')

exports.decode = function decode(image) {
  const { width, height, data } = binding.decode(image)

  return {
    width,
    height,
    data: Buffer.from(data)
  }
}

exports.decodeAnimated = function decodeAnimated(image, opts = {}) {
  const { maxPixels = binding.defaultMaxPixels } = opts

  const decoder = binding.animatedDecoderInit(image, maxPixels)

  const { width, height } = binding.animatedDecoderGetInfo(decoder)

  const frames = {
    next() {
      const frame = binding.animatedDecoderGetNextFrame(
        decoder,
        image // Keep a reference for lifetime management
      )

      if (frame === null) {
        return {
          done: true
        }
      }

      const { timestamp, data } = frame

      return {
        done: false,
        value: {
          width,
          height,
          timestamp,
          data: Buffer.from(data)
        }
      }
    },

    [Symbol.iterator]() {
      return frames
    }
  }

  return {
    width,
    height,
    frames
  }
}

exports.encode = function encode(image, opts = {}) {
  const { colors = binding.maxColors } = opts

  const { width, height, data } = image

  assertCanvas(width, height)
  assertFrame(width, height, data)

  const buffer = binding.encode(data, width, height, clamp(colors, 2, binding.maxColors))

  return Buffer.from(buffer)
}

exports.encodeAnimated = function encodeAnimated(image, opts = {}) {
  const { colors = binding.maxColors, loops = 0 } = opts

  const { width, height } = image

  assertCanvas(width, height)

  const encoder = binding.animatedEncoderInit(width, height, loops)

  try {
    for (const { data, timestamp } of image.frames) {
      assertFrame(width, height, data)

      binding.animatedEncoderAddFrame(encoder, data, clamp(colors, 2, binding.maxColors), timestamp)
    }

    return Buffer.from(binding.animatedEncoderAssemble(encoder))
  } finally {
    binding.animatedEncoderDestroy(encoder)
  }
}

function assertCanvas(width, height) {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0) {
    throw new Error('Width and height must be positive integers')
  }

  if (width * height > binding.maxPixels) {
    throw new Error(`Image exceeds the maximum of ${binding.maxPixels} pixels`)
  }
}

function assertFrame(width, height, data) {
  if (!ArrayBuffer.isView(data) || data instanceof DataView) {
    throw new Error('Image data must be a typed array')
  }

  const expected = width * height * 4

  if (data.byteLength < expected) {
    throw new Error(`Image needs ${expected} bytes of RGBA data, got ${data.byteLength}`)
  }
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value))
}
