# bare-gif

GIF support for Bare.

```
npm i bare-gif
```

## Usage

```js
const gif = require('bare-gif')

const image = require('./my-image.gif', { with: { type: 'binary' } })

const decoded = gif.decode(image)
// {
//   width: 200,
//   height: 400,
//   data: <Buffer>
// }
```

## License

Apache-2.0
