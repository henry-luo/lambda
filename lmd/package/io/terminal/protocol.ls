// Terminal byte protocol. The OS host supplies chunks and deadlines; key
// meaning and incomplete UTF-8/CSI decoder remain in Lambda (S12.1.3).

pub fn new_state() =>
  {pending: [], utf_expected: 0, saw_cr: false, paste: false,
   paste_chunks: null, paste_count: 0}

fn clear_sequence(decoder) =>
  {*: decoder, pending: [], utf_expected: 0, saw_cr: false}

fn result(decoder, events) => {decoder: decoder, events: events}
fn key(key_name) => {kind: "key", key: key_name}
fn text(value) => {kind: "text", text: value}
fn unknown(bytes) => {kind: "unknown", bytes: bytes}
fn bytes_text(bytes) => join([for (byte in bytes) chr(byte)], "")
fn paste_append(decoder, value) =>
  {*: decoder, paste_chunks: {value: value, previous: decoder.paste_chunks},
   paste_count: decoder.paste_count + len(value)}

pn paste_finish(decoder) {
  var chunks = []
  var current = decoder.paste_chunks
  while (current != null) {
    push(chunks, current.value)
    current = current.previous
  }
  {decoder: {*: decoder, paste_chunks: null, paste_count: 0},
   events: if (len(chunks) == 0) []
           else [text(join(reverse(chunks), ""))]}
}

fn csi_params(bytes) {
  let scanned = reduce([{values: [], value: 0, digits: 0, valid: true}, *bytes],
    (part, byte) =>
      // Reject oversized parameters before arithmetic can overflow on input.
      if (byte >= 48 and byte <= 57 and part.value <= 10000)
        {*: part, value: part.value * 10 + byte - 48, digits: part.digits + 1}
      else if (byte == 59 and part.digits > 0)
        {values: [*part.values, part.value], value: 0, digits: 0,
         valid: part.valid}
      else {*: part, valid: false})
  if (len(bytes) == 0) []
  else if (scanned.valid and scanned.digits > 0)
    [*scanned.values, scanned.value]
  else null
}

fn modified_key(key_name, modifier) =>
  if (modifier == 2) {kind: "key", key: key_name, shift: true}
  else if (modifier == 3) {kind: "key", key: key_name, alt: true}
  else if (modifier == 4)
    {kind: "key", key: key_name, shift: true, alt: true}
  else if (modifier == 5) {kind: "key", key: key_name, ctrl: true}
  else if (modifier == 6)
    {kind: "key", key: key_name, shift: true, ctrl: true}
  else if (modifier == 7)
    {kind: "key", key: key_name, alt: true, ctrl: true}
  else if (modifier == 8)
    {kind: "key", key: key_name, shift: true, alt: true, ctrl: true}
  else if (modifier == 1) key(key_name)
  else unknown([])

fn utf8_value(bytes) {
  if (len(bytes) == 2) (bytes[0] - 192) * 64 + (bytes[1] - 128)
  else if (len(bytes) == 3)
    (bytes[0] - 224) * 4096 + (bytes[1] - 128) * 64 + (bytes[2] - 128)
  else
    (bytes[0] - 240) * 262144 + (bytes[1] - 128) * 4096 +
    (bytes[2] - 128) * 64 + (bytes[3] - 128)
}

fn valid_scalar(cp, byte_count) {
  let minimum = if (byte_count == 2) 128 else if (byte_count == 3) 2048 else 65536
  cp >= minimum and cp <= 1114111 and not (cp >= 55296 and cp <= 57343)
}

fn csi_event(params, final_byte) {
  let values = csi_params(params)
  let first = if (values == null or len(values) == 0) 0 else values[0]
  let modifier = if (values != null and len(values) == 2) values[1] else 1
  let named = if (final_byte == 65) "ArrowUp"
              else if (final_byte == 66) "ArrowDown"
              else if (final_byte == 67) "ArrowRight"
              else if (final_byte == 68) "ArrowLeft"
              else if (final_byte == 72) "Home"
              else if (final_byte == 70) "End"
              else if (final_byte == 126 and first == 3) "Delete"
              else if (final_byte == 126 and (first == 1 or first == 7)) "Home"
              else if (final_byte == 126 and (first == 4 or first == 8)) "End"
              else if (final_byte == 126 and first == 5) "PageUp"
              else if (final_byte == 126 and first == 6) "PageDown"
              else if (final_byte == 126 and first == 2 and len(values) == 1) "Insert"
              else null
  if (final_byte == 126 and values == [200]) key("PasteStart")
  else if (final_byte == 126 and values == [201]) key("PasteEnd")
  else if (named != null and values != null and
           ((final_byte != 126 and (len(values) == 0 or first == 1)) or
            (final_byte == 126 and len(values) >= 1)) and
           len(values) <= 2 and modifier >= 1 and modifier <= 8)
    modified_key(named, modifier)
  else unknown([27, 91, *params, final_byte])
}

fn utf_step(decoder, byte) {
  if (byte < 128 or byte > 191) {
    // The replacement consumes only the invalid prefix; retry this byte.
    let next = feed_byte(clear_sequence(decoder), byte)
    result(next.decoder, [text(chr(65533)), *next.events])
  }
  else {
    let pending = [*decoder.pending, byte]
    if (len(pending) < decoder.utf_expected)
      result({*: decoder, pending: pending}, [])
    else {
      let cp = utf8_value(pending)
      result(clear_sequence(decoder),
             [text(if (valid_scalar(cp, len(pending))) chr(cp) else chr(65533))])
    }
  }
}

fn escape_step(decoder, byte) {
  let pending = decoder.pending
  if (len(pending) == 1) {
    if (byte == 91 or byte == 79)
      result({*: decoder, pending: [27, byte]}, [])
    else if (byte >= 32 and byte <= 126)
      if (decoder.paste)
        result(clear_sequence(decoder), [text(bytes_text([27, byte]))])
      else result(clear_sequence(decoder),
                  [{kind: "key", key: chr(byte), alt: true}])
    else {
      let next = feed_byte(clear_sequence(decoder), byte)
      result(next.decoder,
             [if (decoder.paste) text(chr(27)) else key("Escape"),
              *next.events])
    }
  }
  else if (pending[1] == 79) {
    let event = if (byte == 65) key("ArrowUp")
                else if (byte == 66) key("ArrowDown")
                else if (byte == 67) key("ArrowRight")
                else if (byte == 68) key("ArrowLeft")
                else if (byte == 72) key("Home")
                else if (byte == 70) key("End")
                else unknown([*pending, byte])
    result(clear_sequence(decoder),
           [if (decoder.paste) text(bytes_text([*pending, byte])) else event])
  }
  else if (byte >= 64 and byte <= 126) {
    let event = csi_event(slice(pending, 2, len(pending)), byte)
    let next = clear_sequence(decoder)
    if (event.kind == "key" and event.key == "PasteStart" and not decoder.paste)
      result({*: next, paste: true}, [event])
    else if (event.kind == "key" and event.key == "PasteEnd" and decoder.paste)
      result({*: next, paste: false}, [event])
    else if (decoder.paste)
      result(next, [text(bytes_text([*pending, byte]))])
    else result(next, [event])
  }
  else if (byte >= 32 and byte <= 63 and len(pending) < 32)
    result({*: decoder, pending: [*pending, byte]}, [])
  else result(clear_sequence(decoder),
              [if (decoder.paste) text(bytes_text([*pending, byte]))
               else unknown([*pending, byte])])
}

pub fn feed_byte(decoder, byte) {
  // CRLF is one submitted line even when transport chunks split the pair.
  if (decoder.saw_cr and byte == 10)
    result({*: decoder, saw_cr: false}, [])
  else if (decoder.saw_cr)
    feed_byte({*: decoder, saw_cr: false}, byte)
  else if (decoder.utf_expected > 0) utf_step(decoder, byte)
  else if (len(decoder.pending) > 0) escape_step(decoder, byte)
  else if (byte == 27)
    result({*: decoder, pending: [27]}, [])
  else if (byte == 13)
    result({*: decoder, saw_cr: true},
           [if (decoder.paste) text("\n") else key("Enter")])
  else if (byte == 10)
    result(decoder, [if (decoder.paste) text("\n") else key("Enter")])
  else if (decoder.paste and byte < 128)
    result(decoder, [text(chr(byte))])
  else if (byte == 127 or byte == 8) result(decoder, [key("Backspace")])
  else if (byte == 9) result(decoder, [key("Tab")])
  else if (byte >= 1 and byte <= 26)
    result(decoder, [key("Control-" ++ chr(64 + byte))])
  else if (byte >= 32 and byte <= 126) result(decoder, [text(chr(byte))])
  else if (byte >= 194 and byte <= 244) {
    let expected = if (byte <= 223) 2 else if (byte <= 239) 3 else 4
    result({*: decoder, pending: [byte], utf_expected: expected}, [])
  }
  else result(decoder, [text(chr(65533))])
}

pub pn feed(decoder, bytes) {
  var current = decoder
  var events = []
  for (byte in bytes) {
    let was_paste = current.paste
    let stepped = feed_byte(current, byte)
    current = stepped.decoder
    for (event in stepped.events) {
      if (was_paste and event.kind == "text") {
        current = paste_append(current, event.text)
        // Chunk a long paste to bound retained decoder state and undo units.
        if (current.paste_count >= 4096) {
          let flushed = paste_finish(current)
          current = flushed.decoder
          for (part in flushed.events) { push(events, part) }
        }
      }
      if (event.kind == "key" and event.key == "PasteEnd") {
        let flushed = paste_finish(current)
        current = flushed.decoder
        for (part in flushed.events) { push(events, part) }
        push(events, event)
      }
      if (not (was_paste and event.kind == "text") and
          not (event.kind == "key" and event.key == "PasteEnd")) {
        push(events, event)
      }
    }
  }
  {decoder: current, events: events}
}

pub fn flush_pending(decoder) {
  if (decoder.utf_expected > 0)
    if (decoder.paste)
      result(paste_append(clear_sequence(decoder), chr(65533)), [])
    else result(clear_sequence(decoder), [text(chr(65533))])
  else if (len(decoder.pending) == 1 and decoder.pending[0] == 27)
    if (decoder.paste)
      result(paste_append(clear_sequence(decoder), chr(27)), [])
    else result(clear_sequence(decoder), [key("Escape")])
  else if (len(decoder.pending) > 0)
    if (decoder.paste)
      result(paste_append(clear_sequence(decoder), bytes_text(decoder.pending)), [])
    else result(clear_sequence(decoder), [unknown(decoder.pending)])
  else result(decoder, [])
}

pub pn end_input(decoder) {
  let pending = flush_pending(decoder)
  let pasted = paste_finish(pending.decoder)
  result(new_state(), [*pending.events, *pasted.events])
}

pub fn pending_timeout(decoder) =>
  if (len(decoder.pending) == 0) null else 100
