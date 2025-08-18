const binding = require('./binding')

exports.decode = function decode(image) {
  const { width, height, data } = binding.decode(image)

  return {
    width,
    height,
    data: Buffer.from(data)
  }
}

exports.decodeAnimated = function decodeAnimated(image) {
  const { width, height, frames } = binding.decodeAnimated(image)

  return {
    width,
    height,
    frames: frames.map((frame) => {
      const { width, height, timestamp, data } = frame

      return {
        width,
        height,
        timestamp,
        data: Buffer.from(data)
      }
    })
  }
}
