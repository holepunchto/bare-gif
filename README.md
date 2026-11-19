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

See the [`bare-gif` reference](https://docs.pears.com/reference/bare/modules/bare-gif).

## License

Apache-2.0
