// SOAP/Web Service XML schema - Fixed for namespace prefixes
// Tests XML for SOAP messages with complex nested structures

type SoapFault = <soap:Fault
    faultcode: string,                // fault code
    faultstring: string,              // fault message
    faultactor: string?,              // optional fault actor
    detail: string?                   // optional fault details
>

type SoapHeader = <soap:Header
    mustUnderstand: string?,          // optional must understand flag (as string for bool)
    actor: string?,                   // optional actor
    content: string?                  // header content (text content)
>

// Parameter element for SOAP body content
type Parameter = <param
    name: string,                     // parameter name
    type: string,                     // parameter type
    value: string                     // parameter value
>

type SoapBodyContent = <content
    method: string,                   // method name
    namespace: string?,               // optional namespace
    Parameter*                        // method parameters
>

type SoapBody = <soap:Body
    SoapBodyContent                   // content element
>

type Document = <soap:Envelope
    "xmlns:soap": string,             // SOAP namespace attribute
    encodingStyle: string?,           // optional encoding style
    SoapHeader?,                      // optional header element
    SoapBody                          // required body element
>
