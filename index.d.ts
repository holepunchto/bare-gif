import Buffer from 'bare-buffer'

interface GIFImage {
  /** The width of the image in pixels. */
  width: number
  /** The height of the image in pixels. */
  height: number
  /** `width * height * 4` bytes of RGBA pixels. */
  data: Buffer
}

interface GIFFrame extends GIFImage {
  /** Milliseconds from the start of the animation. */
  timestamp: number
}

interface GIFAnimation {
  /** The width of the canvas in pixels. */
  width: number
  /** The height of the canvas in pixels. */
  height: number
  /** The frames of the animation, yielded one at a time. */
  frames: IterableIterator<GIFFrame>
}

interface GIFSourceFrame {
  /** `width * height * 4` bytes of RGBA pixels covering the whole canvas. */
  data: Buffer
  /** Milliseconds from the start of the animation. */
  timestamp: number
}

interface GIFSourceAnimation {
  /** The width of the canvas in pixels. */
  width: number
  /** The height of the canvas in pixels. */
  height: number
  /** The frames of the animation, each covering the whole canvas. */
  frames: Iterable<GIFSourceFrame>
}

interface GIFDecodeAnimatedOptions {
  /**
   * Pixels one decode may produce in total, or `0` for no budget.
   * @default defaultMaxPixels
   */
  maxPixels?: number
}

interface GIFEncodeOptions {
  /**
   * Palette entries, from `2` to `maxColors`.
   * @default maxColors
   */
  colors?: number
}

interface GIFEncodeAnimatedOptions extends GIFEncodeOptions {
  /**
   * Times to repeat the animation, from `0` to `maxLoops`. `0` loops forever.
   * @default 0
   */
  loops?: number
  /**
   * Pixels the whole animation may hold, or `0` for no budget.
   * @default defaultMaxPixels
   */
  maxPixels?: number
}

/**
 * Decode the first frame of a GIF.
 * @param image - The GIF to decode.
 * @returns The frame, as RGBA pixels.
 */
declare function decode(image: Buffer): GIFImage

/**
 * Decode every frame of a GIF. Frames are decoded lazily, as the iterator is walked, and each one
 * carries the whole canvas as it looks at that point rather than only the part that changed.
 * @param image - The GIF to decode.
 * @param opts - Options.
 * @throws If `maxPixels` is not a whole number, or the animation exceeds it.
 */
declare function decodeAnimated(image: Buffer, opts?: GIFDecodeAnimatedOptions): GIFAnimation

/**
 * Encode RGBA pixels as a GIF.
 *
 * A GIF is palette based, so the colours are reduced to at most `maxColors` entries. An image that
 * already fits a palette keeps its colours exactly. It has no alpha channel either, only a
 * transparent palette index, so a pixel with an alpha below 128 becomes fully transparent and every
 * other pixel becomes fully opaque.
 * @param image - The pixels to encode.
 * @param opts - Options.
 * @returns The GIF.
 * @throws If `data` is not a typed array, is shorter than the dimensions, or `width`, `height` or
 * `colors` is outside what a GIF records.
 */
declare function encode(image: GIFImage, opts?: GIFEncodeOptions): Buffer

/**
 * Encode a sequence of RGBA frames as an animated GIF.
 *
 * Every frame covers the whole canvas and carries its own palette, under the same reduction as
 * `encode()`. A frame is shown until the next one's `timestamp`, and the last frame is shown for as
 * long as the one before it.
 * @param image - The frames to encode.
 * @param opts - Options.
 * @returns The GIF.
 * @throws If there are no frames, a frame does not match the canvas, or `loops`, `colors` or
 * `maxPixels` is outside what a GIF records.
 */
declare function encodeAnimated(image: GIFSourceAnimation, opts?: GIFEncodeAnimatedOptions): Buffer

/** Palette entries one colour map holds. */
declare const maxColors: number

/** The widest a screen or image field records. */
declare const maxDimension: number

/** The most repeats a loop record holds. */
declare const maxLoops: number

/** Pixels one image may hold. */
declare const maxFramePixels: number

/** Default budget for a whole decode or animation. */
declare const defaultMaxPixels: number

export {
  decode,
  decodeAnimated,
  encode,
  encodeAnimated,
  maxColors,
  maxDimension,
  maxLoops,
  maxFramePixels,
  defaultMaxPixels,
  type GIFImage,
  type GIFFrame,
  type GIFAnimation,
  type GIFSourceFrame,
  type GIFSourceAnimation,
  type GIFDecodeAnimatedOptions,
  type GIFEncodeOptions,
  type GIFEncodeAnimatedOptions
}
