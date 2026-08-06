// Keep the wrapper in non-macOS root builds so ime_mac.mm supplies its no-op
// attachment symbol; macOS excludes this root copy and builds it as ObjC++.
#include "ime_mac.mm"
