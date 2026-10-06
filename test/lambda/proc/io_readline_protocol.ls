import protocol: lambda.io.terminal.protocol

pn main() {
  let csi0 = protocol.feed(protocol.new_state(), b'\x1B5B')
  let csi1 = protocol.feed(csi0.decoder, b'\x337E')
  print(len(csi0.events) == 0 and csi1.events[0].key == "Delete"); print('\n')

  let backspace = protocol.feed(protocol.new_state(), b'\x7F')
  print(backspace.events[0].key == "Backspace"); print('\n')

  let tab = protocol.feed(protocol.new_state(), b'\x09')
  print(tab.events[0].kind == "key" and tab.events[0].key == "Tab"); print('\n')

  let unicode0 = protocol.feed(protocol.new_state(), b'\xC3')
  let unicode1 = protocol.feed(unicode0.decoder, b'\xA9')
  print(len(unicode0.events) == 0 and unicode1.events[0].text == "é"); print('\n')

  let invalid0 = protocol.feed(protocol.new_state(), b'\xE2')
  let invalid1 = protocol.feed(invalid0.decoder, b'\x41')
  print(invalid1.events[0].text == chr(65533) and invalid1.events[1].text == "A"); print('\n')

  let escape0 = protocol.feed(protocol.new_state(), b'\x1B')
  let escape1 = protocol.flush_pending(escape0.decoder)
  print(escape1.events[0].key == "Escape"); print('\n')

  let alt = protocol.feed(protocol.new_state(), b'\x1B78')
  print(alt.events[0].key == "x" and alt.events[0].alt); print('\n')

  let crlf = protocol.feed(protocol.new_state(), b'\x0D0A')
  print(len(crlf.events) == 1 and crlf.events[0].key == "Enter"); print('\n')
  let cr = protocol.feed(protocol.new_state(), b'\x0D')
  let lf = protocol.feed(cr.decoder, b'\x0A')
  print(len(cr.events) == 1 and len(lf.events) == 0); print('\n')

  let ctrl_left = protocol.feed(protocol.new_state(), b'\x1B5B313B3544')
  print(ctrl_left.events[0].key == "ArrowLeft" and ctrl_left.events[0].ctrl);
  print('\n')
  let shift_delete = protocol.feed(protocol.new_state(), b'\x1B5B333B327E')
  print(shift_delete.events[0].key == "Delete" and
        shift_delete.events[0].shift); print('\n')
  let malformed = protocol.feed(protocol.new_state(), b'\x1B5B313B3B3544')
  print(malformed.events[0].kind == "unknown"); print('\n')
  let oversized = protocol.feed(protocol.new_state(),
                                b'\x1B5B39393939393939393939393944')
  print(oversized.events[0].kind == "unknown"); print('\n')

  let start = protocol.feed(protocol.new_state(), b'\x1B5B3230307E61C3')
  let middle = protocol.feed(start.decoder, b'\xA90D0A621B5B3230')
  let finish = protocol.feed(middle.decoder, b'\x317E')
  print(start.events[0].key == "PasteStart" and start.decoder.paste and
        len(middle.events) == 0 and finish.events[0].text == "aé\nb" and
        finish.events[1].key == "PasteEnd" and not finish.decoder.paste);
  print('\n')

  let escaped = protocol.feed(protocol.new_state(),
                              b'\x1B5B3230307E1B5B411B5B3230317E')
  print(escaped.events[1].kind == "text" and
        escaped.events[1].text == chr(27) ++ "[A"); print('\n')

  let incomplete = protocol.feed(protocol.new_state(), b'\x1B5B3230307E78')
  let ended = protocol.end_input(incomplete.decoder)
  print(ended.events[0].text == "x" and not ended.decoder.paste); print('\n')
}
