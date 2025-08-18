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
