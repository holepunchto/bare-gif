const test = require('brittle')
const gif = require('.')
const { craftGIF } = require('./test/helpers')

const bufferfly = require('./test/fixtures/bufferfly.gif', {
  with: { type: 'binary' }
})

const emptyCanvas = craftGIF({
  canvasWidth: 0,
  canvasHeight: 0,
  frames: [{ width: 1, height: 1 }]
})

const emptyCanvasAndFrame = craftGIF({
  canvasWidth: 0,
  canvasHeight: 0,
  frames: [{ width: 0, height: 0 }]
})

const emptyFrame = craftGIF({
  canvasWidth: 4,
  canvasHeight: 4,
  frames: [{ width: 0, height: 0 }]
})

const oversizedFrame = craftGIF({
  canvasWidth: 4,
  canvasHeight: 4,
  frames: [{ width: 1000, height: 1000 }]
})

const oversizedCanvas = craftGIF({
  canvasWidth: 0xffff,
  canvasHeight: 0xffff,
  frames: [{ width: 1, height: 1 }]
})

const defectiveSecondFrame = craftGIF({
  canvasWidth: 1,
  canvasHeight: 1,
  frames: [
    { width: 1, height: 1 },
    { width: 1000, height: 1000 }
  ]
})

const frameBomb = craftGIF({
  canvasWidth: 2000,
  canvasHeight: 2000,
  frames: new Array(150).fill({ width: 1, height: 1 })
})

const oversizedAnimatedCanvas = craftGIF({
  canvasWidth: 9000,
  canvasHeight: 9000,
  frames: [{ width: 1, height: 1 }]
})

const badExtension = craftGIF({
  canvasWidth: 1,
  canvasHeight: 1,
  frames: [{ width: 1, height: 1 }],
  blockSize: 3
})

const notAGIF = Buffer.from('this is not a gif')
const tooShort = [Buffer.alloc(0), Buffer.from('GIF8')]
const headerOnly = Buffer.from('GIF89a')

test('decode .gif', (t) => {
  t.comment(gif.decode(bufferfly))
})

test('decode animated .gif', (t) => {
  const decoded = gif.decodeAnimated(bufferfly)

  for (const frame of decoded.frames) {
    t.comment(frame)
  }
})

test('decodeAnimated() yields frames lazily', (t) => {
  const decoded = gif.decodeAnimated(bufferfly)

  t.is(typeof decoded.frames.next, 'function')
  t.is(decoded.frames[Symbol.iterator](), decoded.frames)

  const first = decoded.frames.next()

  t.is(first.done, false)
  t.is(first.value.data.byteLength, decoded.width * decoded.height * 4)
})

test('decodeAnimated() is exhausted after the last frame', (t) => {
  const decoded = gif.decodeAnimated(bufferfly)

  t.is([...decoded.frames].length, 22)
  t.is(decoded.frames.next().done, true)
})

test('rejects frame rect that exceeds the canvas', (t) => {
  t.exception(() => gif.decode(oversizedFrame), /defective/i)
})

test('rejects canvas dimensions that exceed the per-frame cap', (t) => {
  t.exception(() => gif.decode(oversizedCanvas), /dimensions exceed maximum/)
})

test('decode() adopts the frame size when the canvas is 0x0', (t) => {
  const decoded = gif.decode(emptyCanvas)

  t.is(decoded.width, 1)
  t.is(decoded.height, 1)
})

test('decodeAnimated() adopts the frame size when the canvas is 0x0', (t) => {
  const decoded = gif.decodeAnimated(emptyCanvas)

  t.is(decoded.width, 1)
  t.is(decoded.height, 1)
  t.is([...decoded.frames].length, 1)
})

test('decode() rejects a 0x0 canvas with a 0x0 frame', (t) => {
  t.exception(() => gif.decode(emptyCanvasAndFrame), /defective/i)
})

test('decodeAnimated() rejects a 0x0 canvas with a 0x0 frame', (t) => {
  t.exception(() => gif.decodeAnimated(emptyCanvasAndFrame), /defective/i)
})

test('decode() rejects a 0x0 frame rect', (t) => {
  t.exception(() => gif.decode(emptyFrame), /bigger than width \* height/)
})

test('decodeAnimated() rejects a 0x0 frame rect', (t) => {
  t.exception(() => gif.decodeAnimated(emptyFrame), /bigger than width \* height/)
})

test('decodeAnimated() rejects an animation that exceeds the total pixel budget', (t) => {
  t.exception(() => [...gif.decodeAnimated(frameBomb).frames], /exceeds maximum decoded size/)
})

test('decodeAnimated() streams past the budget when it is lifted', (t) => {
  const { frames } = gif.decodeAnimated(frameBomb, { maxPixels: 0 })

  let count = 0

  for (const frame of frames) {
    t.is(frame.data.byteLength, 2000 * 2000 * 4)
    count++
  }

  t.is(count, 150)
})

test('decodeAnimated() still bounds a single frame when the budget is lifted', (t) => {
  t.exception(
    () => [...gif.decodeAnimated(oversizedAnimatedCanvas, { maxPixels: 0 }).frames],
    /dimensions exceed maximum/
  )
})

test('decodeAnimated() honours a budget below the default', (t) => {
  const { frames } = gif.decodeAnimated(frameBomb, { maxPixels: 2000 * 2000 * 2 })

  let count = 0

  t.exception(() => {
    for (const frame of frames) count++
  }, /exceeds maximum decoded size/)

  t.is(count, 2)
})

test('decodeAnimated() stays within the budget when iteration stops early', (t) => {
  const { frames } = gif.decodeAnimated(frameBomb)

  const taken = []

  for (const frame of frames) {
    taken.push(frame)
    if (taken.length === 3) break
  }

  t.is(taken.length, 3)
})

test('decodeAnimated() rejects a defective frame after the first', (t) => {
  t.exception(() => [...gif.decodeAnimated(defectiveSecondFrame).frames], /defective/i)
})

test('decode() stops at the first frame and ignores a later defective one', (t) => {
  const decoded = gif.decode(defectiveSecondFrame)

  t.is(decoded.width, 1)
  t.is(decoded.height, 1)
})

test('decode() rejects a graphics extension with a bad block size', (t) => {
  t.exception(() => gif.decode(badExtension), /defective/i)
})

test('decodeAnimated() rejects a graphics extension with a bad block size', (t) => {
  t.exception(() => gif.decodeAnimated(badExtension), /defective/i)
})

test('decode() rejects input that is not a GIF', (t) => {
  t.exception(() => gif.decode(notAGIF), /not in GIF format/)
})

test('decodeAnimated() rejects input that is not a GIF', (t) => {
  t.exception(() => gif.decodeAnimated(notAGIF), /not in GIF format/)
})

test('decode() rejects a buffer too short to hold a header', (t) => {
  for (const short of tooShort) {
    t.exception(() => gif.decode(short), /Failed to read/)
  }
})

test('decodeAnimated() rejects a buffer too short to hold a header', (t) => {
  for (const short of tooShort) {
    t.exception(() => gif.decodeAnimated(short), /Failed to read/)
  }
})

test('decode() rejects a header with no screen descriptor', (t) => {
  t.exception(() => gif.decode(headerOnly), /No screen descriptor/)
})

test('decodeAnimated() rejects a header with no screen descriptor', (t) => {
  t.exception(() => gif.decodeAnimated(headerOnly), /No screen descriptor/)
})

test('encode() round trips a solid image', (t) => {
  const image = solid(8, 4, [255, 0, 0, 255])

  const decoded = gif.decode(gif.encode(image))

  t.is(decoded.width, 8)
  t.is(decoded.height, 4)
  t.alike([...decoded.data.subarray(0, 4)], [255, 0, 0, 255])
})

test('encode() keeps every distinct colour it has room for', (t) => {
  const image = gradient(16, 16)

  const decoded = gif.decode(gif.encode(image))

  t.is(countColors(decoded.data), countColors(image.data))
})

test('encode() spends no palette entry on a transparent pixel', (t) => {
  const image = solid(8, 1, [0, 0, 0, 0])
  const colours = [
    [255, 0, 0],
    [255, 127, 0],
    [0, 255, 0],
    [0, 0, 255]
  ]

  for (let i = 0; i < colours.length; i++) {
    image.data[i * 4] = colours[i][0]
    image.data[i * 4 + 1] = colours[i][1]
    image.data[i * 4 + 2] = colours[i][2]
    image.data[i * 4 + 3] = 255
  }

  const decoded = gif.decode(gif.encode(image, { colors: 5 }))

  for (let i = 0; i < colours.length; i++) {
    t.alike([...decoded.data.subarray(i * 4, i * 4 + 3)], colours[i])
  }
})

test('decode() then encode() loses nothing', (t) => {
  const decoded = gif.decodeAnimated(bufferfly)
  const frames = [...decoded.frames]

  const encoded = gif.encodeAnimated({
    width: decoded.width,
    height: decoded.height,
    frames
  })

  const again = [...gif.decodeAnimated(encoded).frames]

  for (let i = 0; i < frames.length; i++) {
    t.alike(frames[i].data, again[i].data, `frame ${i} is unchanged`)
  }
})

test('encode() reduces the palette to the colours asked for', (t) => {
  const image = gradient(16, 16)

  const decoded = gif.decode(gif.encode(image, { colors: 4 }))

  t.ok(countColors(decoded.data) <= 4)
})

test('encode() keeps a fully transparent pixel transparent', (t) => {
  const image = solid(2, 1, [0, 0, 255, 255])

  image.data.fill(0, 4, 8)

  const decoded = gif.decode(gif.encode(image))

  t.is(decoded.data[7], 0, 'second pixel is transparent')
  t.is(decoded.data[3], 255, 'first pixel is opaque')
})

test('encode() rejects an image with no pixels', (t) => {
  t.exception(() => gif.encode(solid(0, 0, [0, 0, 0, 0])), /positive integers/i)
})

test('encode() rejects data shorter than its dimensions', (t) => {
  t.exception(() => gif.encode({ width: 4, height: 4, data: Buffer.alloc(8) }), /needs 64 bytes/i)
})

test('encode() reads a view that does not start on a word boundary', (t) => {
  const image = solid(4, 4, [255, 0, 0, 255])
  const skewed = skew(image.data)

  t.is(skewed.byteOffset % 4, 1, 'the view really is off a word boundary')
  t.alike(gif.encode({ width: 4, height: 4, data: skewed }), gif.encode(image))
})

test('encodeAnimated() reads a view that does not start on a word boundary', (t) => {
  const image = solid(4, 4, [255, 0, 0, 255])

  const encoded = gif.encodeAnimated({
    width: 4,
    height: 4,
    frames: [{ data: skew(image.data), timestamp: 0 }]
  })

  const frame = [...gif.decodeAnimated(encoded).frames][0]

  t.alike(frame.data, image.data)
})

test('encode() rejects data that is not a typed array', (t) => {
  for (const data of [
    { byteLength: 16 },
    [0, 0, 0, 0],
    'xxxx',
    new ArrayBuffer(16),
    new DataView(new ArrayBuffer(16))
  ]) {
    t.exception(() => gif.encode({ width: 2, height: 2, data }), /must be a typed array/i)
  }
})

test('encodeAnimated() releases the encoder when a frame is rejected', (t) => {
  t.exception(
    () =>
      gif.encodeAnimated({
        width: 2,
        height: 2,
        frames: [
          { data: solid(2, 2, [255, 0, 0, 255]).data, timestamp: 0 },
          { data: 'not a frame', timestamp: 100 }
        ]
      }),
    /must be a typed array/i
  )
})

test('the limits a caller can check against are the ones enforced', (t) => {
  t.exception(
    () => gif.encode(solid(1, 1, [0, 0, 0, 255]), { colors: gif.maxColors + 1 }),
    new RegExp(`between 2 and ${gif.maxColors}`)
  )

  t.exception(
    () => gif.encodeAnimated({ width: 1, height: 1, frames: [] }, { loops: gif.maxLoops + 1 }),
    new RegExp(`between 0 and ${gif.maxLoops}`)
  )

  t.exception(
    () =>
      gif.encode({
        width: gif.maxDimension + 1,
        height: 1,
        data: Buffer.alloc(4)
      }),
    new RegExp(`at most ${gif.maxDimension}`)
  )
})

test('encode() rejects a colour count that is not a whole number in range', (t) => {
  const image = solid(1, 1, [0, 0, 0, 255])

  for (const colors of [NaN, '256', 1000, 1, 1.5, null]) {
    t.exception(() => gif.encode(image, { colors }), /colors must be an integer between 2 and 256/)
  }
})

test('encodeAnimated() rejects a loop count outside what a GIF records', (t) => {
  const frames = [{ data: solid(1, 1, [0, 0, 0, 255]).data, timestamp: 0 }]

  for (const loops of [65536, -1, NaN, 1.5]) {
    t.exception(
      () => gif.encodeAnimated({ width: 1, height: 1, frames }, { loops }),
      /loops must be an integer between 0 and 65535/
    )
  }

  t.execution(() => gif.encodeAnimated({ width: 1, height: 1, frames }, { loops: 65535 }))
})

test('encodeAnimated() rejects a timestamp that is not a whole number', (t) => {
  for (const timestamp of ['100', NaN, -1, undefined]) {
    t.exception(
      () =>
        gif.encodeAnimated({
          width: 1,
          height: 1,
          frames: [{ data: solid(1, 1, [0, 0, 0, 255]).data, timestamp }]
        }),
      /timestamp must be an integer/
    )
  }
})

test('encodeAnimated() round trips frames and their timing', (t) => {
  const encoded = gif.encodeAnimated({
    width: 4,
    height: 4,
    frames: [
      { data: solid(4, 4, [255, 0, 0, 255]).data, timestamp: 0 },
      { data: solid(4, 4, [0, 255, 0, 255]).data, timestamp: 100 },
      { data: solid(4, 4, [0, 0, 255, 255]).data, timestamp: 200 }
    ]
  })

  const decoded = gif.decodeAnimated(encoded)
  const frames = [...decoded.frames]

  t.is(decoded.width, 4)
  t.is(decoded.height, 4)
  t.is(frames.length, 3)
  t.alike([...frames[0].data.subarray(0, 4)], [255, 0, 0, 255])
  t.alike([...frames[1].data.subarray(0, 4)], [0, 255, 0, 255])
  t.is(frames[1].timestamp, 100)
})

test('encode() stamps GIF89a, the version its extensions need', (t) => {
  const still = gif.encode(solid(2, 2, [255, 0, 0, 255]))
  const animated = gif.encodeAnimated({
    width: 2,
    height: 2,
    frames: [{ data: solid(2, 2, [255, 0, 0, 255]).data, timestamp: 0 }]
  })

  t.is(still.subarray(0, 6).toString(), 'GIF89a')
  t.is(animated.subarray(0, 6).toString(), 'GIF89a')
})

test('encodeAnimated() keeps a transparent frame transparent', (t) => {
  const encoded = gif.encodeAnimated({
    width: 2,
    height: 1,
    frames: [
      { data: solid(2, 1, [255, 0, 0, 255]).data, timestamp: 0 },
      { data: solid(2, 1, [0, 0, 0, 0]).data, timestamp: 100 }
    ]
  })

  const frames = [...gif.decodeAnimated(encoded).frames]

  t.alike([...frames[0].data.subarray(0, 4)], [255, 0, 0, 255])
  t.alike(
    [...frames[1].data.subarray(0, 4)],
    [0, 0, 0, 0],
    'the opaque frame is disposed before the transparent one'
  )
})

test('encodeAnimated() keeps frame timing from drifting', (t) => {
  const frame = solid(1, 1, [0, 0, 0, 255]).data
  const timestamps = [0, 33, 67, 100, 133]

  const encoded = gif.encodeAnimated({
    width: 1,
    height: 1,
    frames: timestamps.map((timestamp) => ({ data: frame, timestamp }))
  })

  const decoded = [...gif.decodeAnimated(encoded).frames].map((f) => f.timestamp)

  for (let i = 0; i < timestamps.length; i++) {
    t.ok(Math.abs(decoded[i] - timestamps[i]) <= 5, `frame ${i} within 5ms`)
  }
})

test('encodeAnimated() caps a delay at the widest the format records', (t) => {
  const frame = solid(1, 1, [0, 0, 0, 255]).data

  const encoded = gif.encodeAnimated({
    width: 1,
    height: 1,
    frames: [
      { data: frame, timestamp: 0 },
      { data: frame, timestamp: 700000 }
    ]
  })

  const decoded = [...gif.decodeAnimated(encoded).frames].map((f) => f.timestamp)

  t.is(decoded[1], 0xffff * 10, 'clamped rather than wrapped')
})

test('encode() rejects a side wider than the format records', (t) => {
  t.exception(
    () => gif.encode({ width: 70000, height: 1, data: Buffer.alloc(280000) }),
    /at most 65535/
  )
  t.exception(
    () => gif.encode({ width: 1, height: 70000, data: Buffer.alloc(280000) }),
    /at most 65535/
  )
})

test('a budget that is not a whole number never passes for no budget', (t) => {
  const frames = [{ data: solid(1, 1, [0, 0, 0, 255]).data, timestamp: 0 }]

  for (const maxPixels of [null, NaN, '100', -1, 1.5]) {
    t.exception(
      () => [...gif.decodeAnimated(bufferfly, { maxPixels }).frames],
      /maxPixels must be an integer/
    )

    t.exception(
      () => gif.encodeAnimated({ width: 1, height: 1, frames }, { maxPixels }),
      /maxPixels must be an integer/
    )
  }

  t.is(
    [...gif.decodeAnimated(bufferfly, { maxPixels: 0 }).frames].length,
    22,
    'zero still lifts the budget on purpose'
  )

  t.execution(() => gif.encodeAnimated({ width: 1, height: 1, frames }, { maxPixels: 0 }))
})

test('encodeAnimated() stops at the budget its own decoder honours', (t) => {
  const frames = Array.from({ length: 100 }, (_, i) => ({
    data: Buffer.alloc(1024 * 1024 * 4),
    timestamp: i * 100
  }))

  t.exception(
    () => gif.encodeAnimated({ width: 1024, height: 1024, frames }),
    /Animation exceeds the maximum/
  )

  t.execution(
    () =>
      gif.encodeAnimated({ width: 1024, height: 1024, frames }, { maxPixels: 200 * 1024 * 1024 }),
    'the budget lifts the same way it does on decode'
  )
})

test('encodeAnimated() rejects an animation with no frames', (t) => {
  t.exception(() => gif.encodeAnimated({ width: 4, height: 4, frames: [] }), /at least one frame/)

  t.exception(
    () => gif.encodeAnimated({ width: 4, height: 4, frames: [][Symbol.iterator]() }),
    /at least one frame/,
    'an exhausted iterator is caught too'
  )
})

test('encodeAnimated() rejects a frame smaller than the canvas', (t) => {
  t.exception(
    () =>
      gif.encodeAnimated({
        width: 4,
        height: 4,
        frames: [{ data: solid(2, 2, [0, 0, 0, 255]).data, timestamp: 0 }]
      }),
    /needs 64 bytes/i
  )
})

test('encode() rejects a canvas beyond the pixel budget', (t) => {
  t.exception(
    () => gif.encode({ width: 1 << 15, height: 1 << 15, data: Buffer.alloc(0) }),
    /maximum of \d+ pixels/i
  )
})

test('encodeAnimated() round trips a real animation', (t) => {
  const decoded = gif.decodeAnimated(bufferfly)
  const frames = [...decoded.frames]

  const encoded = gif.encodeAnimated({
    width: decoded.width,
    height: decoded.height,
    frames
  })

  const again = gif.decodeAnimated(encoded)
  const round = [...again.frames]

  t.is(again.width, decoded.width)
  t.is(again.height, decoded.height)
  t.is(round.length, frames.length)
  t.is(round[1].timestamp, frames[1].timestamp)
})

function solid(width, height, [red, green, blue, alpha]) {
  const data = Buffer.alloc(width * height * 4)

  for (let i = 0; i < data.byteLength; i += 4) {
    data[i] = red
    data[i + 1] = green
    data[i + 2] = blue
    data[i + 3] = alpha
  }

  return { width, height, data }
}

function gradient(width, height) {
  const data = Buffer.alloc(width * height * 4)

  for (let i = 0; i < width * height; i++) {
    data[i * 4] = i % 256
    data[i * 4 + 1] = 0
    data[i * 4 + 2] = 0
    data[i * 4 + 3] = 255
  }

  return { width, height, data }
}

function skew(data) {
  const padded = Buffer.alloc(data.byteLength + 1)

  data.copy(padded, 1)

  return padded.subarray(1)
}

function countColors(data) {
  const seen = new Set()

  for (let i = 0; i < data.byteLength; i += 4) {
    seen.add(data.readUInt32LE(i))
  }

  return seen.size
}
