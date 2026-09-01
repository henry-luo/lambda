// Lambda `dom` package — UA behavior for HTML elements, in Lambda script.
// Radiant loads this once per document; every template it declares registers
// as a behavior template (attached at dispatch time to elements it did not
// produce), never as an author template selected by apply().
import .form
import .navigation
