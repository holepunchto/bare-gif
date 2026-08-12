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

test('rejects frame rect that exceeds the canvas', (t) => {
  t.exception(() => gif.decode(oversizedFrame), /defective/i)
})

test('rejects canvas dimensions that exceed the allocation cap', (t) => {
  t.exception(() => gif.decode(oversizedCanvas), /memory/i)
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
  t.is(decoded.frames.length, 1)
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

test('decodeAnimated() rejects a defective frame after the first', (t) => {
  t.exception(() => gif.decodeAnimated(defectiveSecondFrame), /defective/i)
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
