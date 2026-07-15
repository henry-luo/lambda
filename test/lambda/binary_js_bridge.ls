import .binary_bridge

let source = b'\xDEADBEEF'
let buffer_copy = bufferCopyAndMutate(source)
let uint8_copy = uint8CopyAndMutate(source)
let from_copy = uint8From(source)
let clamped_copy = clampedCopy(source)
let middle_view = middleDataView(source)
let shared_view = sharedView(source)
let shared_snapshot = binary(shared_view)
let mutated_shared_view = mutateSiblingViews(shared_view)
let middle_uint8_view = middleUint8View(source)
let shared_array_buffer_view = sharedArrayBufferView(source)
let shared_array_buffer_snapshot = binary(shared_array_buffer_view)
let mutated_shared_array_buffer = mutateSharedArrayBufferView(shared_array_buffer_view)

[
  source,
  binary(buffer_copy),
  binary(uint8_copy),
  binary(from_copy),
  binary(clamped_copy),
  binary(middle_view),
  shared_snapshot,
  binary(mutated_shared_view),
  binary(middle_uint8_view),
  shared_array_buffer_snapshot,
  binary(mutated_shared_array_buffer),
  source
]
