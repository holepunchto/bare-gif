// Minimal GIF89a builder used to craft adversarial inputs. The LZW payload
// only ever decodes a single pixel, so a frame larger than 1x1 runs out of
// data before it completes.

const u16 = (n) => Buffer.from([n & 0xff, (n >> 8) & 0xff])

exports.craftGIF = function craftGIF({ canvasWidth, canvasHeight, frames, blockSize = 4 }) {
  // Graphics control extension. Its block size must be 4; anything else is
  // rejected as defective.
  const extension = Buffer.concat([
    Buffer.from([0x21, 0xf9, blockSize]),
    Buffer.alloc(blockSize),
    Buffer.from([0x00]) // block terminator
  ])

  const frame = ({ width, height }) =>
    Buffer.concat([
      Buffer.from([0x2c]), // image descriptor
      u16(0), // Left
      u16(0), // Top
      u16(width),
      u16(height),
      Buffer.from([0x00]), // no LCT, not interlaced
      Buffer.from([0x02, 0x02, 0x44, 0x01, 0x00]) // one pixel of LZW data
    ])

  return Buffer.concat([
    Buffer.from('GIF89a'),
    u16(canvasWidth),
    u16(canvasHeight),
    Buffer.from([0x80, 0x00, 0x00]), // GCT present, 2 colors
    Buffer.from([0x00, 0x00, 0x00, 0xff, 0xff, 0xff]), // GCT entries
    extension,
    ...frames.map(frame),
    Buffer.from([0x3b]) // trailer
  ])
}
