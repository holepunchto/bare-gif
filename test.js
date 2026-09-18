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
