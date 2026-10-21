// preserve the unhandled button error with its owning component; the full
// facade adds unrelated cold JIT compilation to this teardown regression.
import button: lambda.ui.dtna.button
button.descriptor({size:'invalid'}, "Invalid")^
