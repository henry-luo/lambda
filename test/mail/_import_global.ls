// An unaliased import of lambda.io.mail is refused: its open and send would
// shadow the built-in ones.
import lambda.io.mail

pn main() {
    print("unreachable\n")
}
