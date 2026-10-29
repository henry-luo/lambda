// LR03-40 remaining format gap: Mark lacks a ruled namespace URI encoding.
let value = parse("<r xmlns:p=\"urn:lambda:pending\" p:a=\"x\"/>",'xml')^;
parse(format(value,'mark')^,'mark')^ == value
