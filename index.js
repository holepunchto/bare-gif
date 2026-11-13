const binding = require('./binding')

exports.maxColors = binding.maxColors
exports.maxDimension = binding.maxDimension
exports.maxLoops = binding.maxLoops
exports.maxFramePixels = binding.maxFramePixels
exports.defaultMaxPixels = binding.defaultMaxPixels

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

  assertInteger('maxPixels', maxPixels, 0, Number.MAX_SAFE_INTEGER)

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
  assertInteger('colors', colors, 2, binding.maxColors)

  const buffer = binding.encode(data, width, height, colors)

  return Buffer.from(buffer)
}

exports.encodeAnimated = function encodeAnimated(image, opts = {}) {
  const { colors = binding.maxColors, loops = 0, maxPixels = binding.defaultMaxPixels } = opts

  const { width, height } = image

  assertCanvas(width, height)
  assertInteger('colors', colors, 2, binding.maxColors)
  assertInteger('loops', loops, 0, binding.maxLoops)
  assertInteger('maxPixels', maxPixels, 0, Number.MAX_SAFE_INTEGER)

  const encoder = binding.animatedEncoderInit(width, height, loops)

  try {
    let count = 0
    let pixels = 0

    for (const { data, timestamp } of image.frames) {
      assertFrame(width, height, data)
      assertInteger('timestamp', timestamp, 0, Number.MAX_SAFE_INTEGER)

      pixels += width * height

      if (maxPixels > 0 && pixels > maxPixels) {
        throw new Error(`Animation exceeds the maximum of ${maxPixels} pixels`)
      }

      binding.animatedEncoderAddFrame(encoder, data, colors, timestamp)

      count++
    }

    if (count === 0) {
      throw new Error('Animation needs at least one frame')
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

  if (width > binding.maxDimension || height > binding.maxDimension) {
    throw new Error(
      `Width and height must be at most ${binding.maxDimension}, the widest a GIF can record`
    )
  }

  if (width * height > binding.maxFramePixels) {
    throw new Error(`Image exceeds the maximum of ${binding.maxFramePixels} pixels`)
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

function assertInteger(name, value, min, max) {
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`${name} must be an integer between ${min} and ${max}`)
  }
}
