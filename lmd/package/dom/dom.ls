// Lambda `dom` package — UA behavior for HTML elements, in Lambda script.
// Radiant loads this once per document; every template it declares registers
// as a behavior template (attached at dispatch time to elements it did not
// produce), never as an author template selected by apply().
// The engine module is imported for its *loading*, not its API: the package no
// longer calls a single radiant.* function, but the module's init is what binds
// the host API the engine-side wrappers run through, and the template and event
// machinery the package's views depend on comes with it. Dropping this import
// from every file left the module unloaded and the event cascade dead.
import radiant
import .form
import .navigation
import .focus
