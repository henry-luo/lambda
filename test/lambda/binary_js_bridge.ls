import .binary_bridge

let source = b'\xDEADBEEF'
let buffer_copy = bufferCopyAndMutate(source)
let uint8_copy = uint8CopyAndMutate(source)
let from_copy = uint8From(source)
let clamped_copy = clampedCopy(source)
let middle_view = middleDataView(source)

[
  source,
  binary(buffer_copy),
  binary(uint8_copy),
  binary(from_copy),
  binary(clamped_copy),
  binary(middle_view),
  source
]
