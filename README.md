# bare-gif

GIF support for Bare.

```
npm i bare-gif
```

## Usage

```js
const gif = require('bare-gif')

const image = require('./my-image.gif', { with: { type: 'binary' } })

const decoded = gif.decodeAnimated(image)
```

## API

### `gif.decode(image)`

Decodes the first frame of a GIF buffer. Returns `{ width, height, data }`, where `data` is a `Buffer` of `width * height * 4` bytes of RGBA pixels.

### `gif.decodeAnimated(image[, options])`

Decodes every frame of a GIF buffer. Returns `{ width, height, frames }`, where `frames` is an iterator that yields `{ width, height, timestamp, data }` one frame at a time. `timestamp` is in milliseconds from the start of the animation and `data` is a `Buffer` of `width * height * 4` bytes of RGBA pixels, the whole canvas as it looks at that point.

Options include:

```js
{
  maxPixels: 67108864 // Pixels one decode may produce in total
}
```

### `gif.encode(image[, options])`

Encodes `{ width, height, data }`, with `data` holding `width * height * 4` bytes of RGBA pixels. Returns a `Buffer` of GIF bytes.

A GIF is palette based, so the colours are reduced to at most 256 entries. It has no alpha channel either, only a transparent palette index, so a pixel with an alpha below 128 becomes fully transparent and every other pixel becomes fully opaque.

Options include:

```js
{
  colors: 256 // Palette entries, 2 to 256
}
```

### `gif.encodeAnimated(image[, options])`

Encodes `{ width, height, frames }`, where `frames` is an iterable of `{ data, timestamp }`. Every frame covers the whole canvas and `timestamp` is in milliseconds from the start of the animation. The last frame is shown for as long as the one before it. Returns a `Buffer` of GIF bytes.

Each frame carries its own palette, under the same reduction as `encode()`.

Options include:

```js
{
  colors: 256, // Palette entries per frame, 2 to 256
  loops: 0 // Times to repeat, 0 to loop forever
}
```

## License

Apache-2.0
