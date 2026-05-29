const test = require('brittle')
const gif = require('.')

test('decode .gif', (t) => {
  const image = require('./test/fixtures/bufferfly.gif', {
    with: { type: 'binary' }
  })

  t.comment(gif.decode(image))
})

test('decode animated .gif', (t) => {
  const image = require('./test/fixtures/bufferfly.gif', {
    with: { type: 'binary' }
  })

  const decoded = gif.decodeAnimated(image)

  for (const frame of decoded.frames) {
    t.comment(frame)
  }
})

// Minimal GIF89a builder used to craft adversarial inputs for the bounds and
// allocation-cap checks below.
function craftGIF({ canvasWidth, canvasHeight, frameWidth, frameHeight }) {
  const u16 = (n) => Buffer.from([n & 0xff, (n >> 8) & 0xff])

  return Buffer.concat([
    Buffer.from('GIF89a'),
    u16(canvasWidth),
    u16(canvasHeight),
    Buffer.from([0x80, 0x00, 0x00]), // GCT present, 2 colors
    Buffer.from([0x00, 0x00, 0x00, 0xff, 0xff, 0xff]), // GCT entries
    Buffer.from([0x2c]), // image descriptor
    u16(0), // Left
    u16(0), // Top
    u16(frameWidth),
    u16(frameHeight),
    Buffer.from([0x00]), // no LCT, not interlaced
    Buffer.from([0x02, 0x02, 0x44, 0x01, 0x00]), // tiny LZW data
    Buffer.from([0x3b]) // trailer
  ])
}

test('rejects frame rect that exceeds the canvas', (t) => {
  const malicious = craftGIF({
    canvasWidth: 4,
    canvasHeight: 4,
    frameWidth: 1000,
    frameHeight: 1000
  })

  t.exception(() => gif.decode(malicious), /defective/i)
})

test('rejects canvas dimensions that exceed the allocation cap', (t) => {
  const malicious = craftGIF({
    canvasWidth: 0xffff,
    canvasHeight: 0xffff,
    frameWidth: 1,
    frameHeight: 1
  })

  t.exception(() => gif.decode(malicious), /memory/i)
})
