workspace "Include policy" {
    !include "https://example.com/workspace.dsl"
    !include ../basic.dsl
    !include missing.dsl
}
