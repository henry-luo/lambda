// Wrapper TU for the radiant static library, whose source pattern is *.cpp and so
// misses app_icon_mac.mm; it is compiled there as Objective-C++. The macOS root
// build excludes this copy and compiles the .mm directly. Empty on other platforms.
#include "app_icon_mac.mm"
