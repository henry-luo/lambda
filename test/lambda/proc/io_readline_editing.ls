import rl: lambda.io.terminal

pn main() {
  let frame = {prompt: "> ", text: "éclair 界_foo!"}
  let forward = rl.word_forward(frame.text, 0)
  let backward = rl.word_backward(frame.text, len(frame.text))
  let killed = rl.kill_word_backward(frame, len(frame.text))
  let tail = rl.kill_to_end(frame, 7)
  let swapped = rl.transpose_chars({prompt: "> ", text: "a界é"}, 2)
  let erased = rl.delete_range(frame, 0, 2)
  print(forward == 7); print('\n')
  print(backward == 7); print('\n')
  print(killed.killed == "界_foo!" and killed.frame.text == "éclair "); print('\n')
  print(tail.killed == "界_foo!"); print('\n')
  print(swapped.frame.text == "aé界" and swapped.caret == 3); print('\n')
  print(erased.frame.text == "lair 界_foo!" and erased.caret == 0); print('\n')
}
