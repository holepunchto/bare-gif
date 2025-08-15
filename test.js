const test = require('brittle')
const gif = require('.')

test('decode .gif', (t) => {
  const image = require('./test/fixtures/bufferfly.gif', {
    with: { type: 'binary' }
  })

  t.comment(gif.decode(image))
})
