// Internal converter process: stdout is a single JSON reply, including recoverable import failures.
import storage: .mod_io

pn main() {
    let reply = storage.open_reply(sys.proc.self.env.LAMBDA_WORD_OPEN_PATH#) ^ { {error:^.message} }
    print(format(reply,'json')^)
}
