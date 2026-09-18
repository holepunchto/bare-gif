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
