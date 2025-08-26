// SOAP/Web Service XML schema
// Tests XML for SOAP messages with complex nested structures
// Note: Using unqualified names, assuming validator strips namespaces

// Root document type - must be first to be recognized as root
type Document = <Envelope
    encodingStyle: string?,           // optional encoding style
    SoapHeader?,                      // optional header  
    SoapBody                          // required body
>

type SoapHeader = <Header
    mustUnderstand: bool?,            // optional must understand flag
    actor: string?,                   // optional actor
    content: string?                  // header content
>

type SoapBodyContent = <content
    method: string,                   // method name
    namespace: string?,               // optional namespace
    parameters: Parameter*            // method parameters
>

type Parameter = <param
    name: string,                     // parameter name
    type: string,                     // parameter type
    value: string                     // parameter value
>

type SoapBody = <Body
    content: SoapBodyContent | SoapFault  // either content or fault
>

type SoapFault = <Fault
    faultcode: string,                // fault code
    faultstring: string,              // fault message
    faultactor: string?,              // optional fault actor
    detail: string?                   // optional fault details
>
