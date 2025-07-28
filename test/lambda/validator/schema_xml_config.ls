// Configuration file XML schema
// Tests XML for configuration files with typed attributes

type ConfigProperty = <property
    name: string,                     // property name
    value: string,                    // property value
    type: string?                     // optional type (string, int, bool, etc.)
>

type ConfigSection = <section
    name: string,                     // section name
    enabled: bool?,                   // optional enabled flag
    properties: ConfigProperty+,      // one or more properties
    subsections: ConfigSection*       // nested subsections
>

type Document = <configuration
    version: string,                  // required version
    schema: string?,                  // optional schema reference
    sections: ConfigSection+          // one or more sections
>
