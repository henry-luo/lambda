// Configuration file XML schema - Fixed
// Tests XML for configuration files with typed attributes
// Features: one-or-more occurrences

// Root configuration element - defined first to be recognized as root
type Document = <configuration
    version: string,                  // required version
    schema: string?;                  // optional schema reference
    any*                              // allow any child elements (open content model)
>

type ConfigSection = <section
    name: string,                     // section name
    enabled: string?;                 // optional enabled flag (as string for bool)
    ConfigProperty+                   // one or more properties (demonstrates one-or-more occurrences)
>

type ConfigProperty = <property
    name: string,                     // property name
    value: string,                    // property value
    type: string?                     // optional type (string, int, bool, etc.)
>
