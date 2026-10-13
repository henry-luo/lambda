// D5.3.3: each converted option stays rooted while the next conversion allocates.
let xml=parse("<root><value>42</value></root>",{type:'xml',flavor:'strict'})^
let markup=parse("# Heading",{type:'markup',flavor:'commonmark'})^;
[name(xml)=='document',xml[0][0][0]=="42",name(markup)=='doc']
