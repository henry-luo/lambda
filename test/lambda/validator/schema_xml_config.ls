// Configuration file XML schema - Fixed
// Tests XML for configuration files with typed attributes

// Root configuration element - defined first to be recognized as root
type Document = <configuration
    version: string,                  // required version
    schema: string?,                  // optional schema reference
    ConfigSection+                    // one or more sections
>

type ConfigSection = <section
    name: string,                     // section name
    enabled: string?,                 // optional enabled flag (as string for bool)
    ConfigProperty*,                  // zero or more properties
    ConfigSection*                    // nested subsections  
>

type ConfigProperty = <property
    name: string,                     // property name
    value: string,                    // property value
    type: string?                     // optional type (string, int, bool, etc.)
>
