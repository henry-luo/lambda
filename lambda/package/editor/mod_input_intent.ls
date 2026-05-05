import .mod_commands

pub fn dispatch_intent(state, ev) =>
  if (ev.input_type == "insertText") cmd_insert_text(state, ev.data)
  else if (ev.input_type == "insertParagraph") cmd_split_block(state)
  else if (ev.input_type == "deleteContentBackward") cmd_delete_backward(state)
  else if (ev.input_type == "deleteContentForward") cmd_delete_forward(state)
  else if (ev.input_type == "formatBold") cmd_format_bold(state)
  else if (ev.input_type == "formatItalic") cmd_format_italic(state)
  else if (ev.input_type == "formatUnderline") cmd_format_underline(state)
  else null
