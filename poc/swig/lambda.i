%module lambda              // defines the module name (used in generated files)

// %{
// #include "example.h"     // verbatim output into the generated wrapper
// %}

%include "lambda.hpp"       // tells SWIG to parse and wrap the contents of lambda.hpp
