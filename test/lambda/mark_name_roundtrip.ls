let values = [
    parse("{\"xml:space\": \"preserve\", \"has space\": 1, \"1digit\": 2, \"\": 3, \"quote\\\"slash\\\\\": 4, \"line\\nkey\": 5, \"data-id\": 6, \"λ\": 7}",'json')^,
    <'svg:use' 'xlink:href':"#sample", 'xml:space':"preserve", "sample">,
    parse("<svg xmlns:xlink=\"http://www.w3.org/1999/xlink\"><text xml:space=\"preserve\">Chart</text></svg>",'xml')^
];
[for (value in values) parse(format(value,'mark')^,'mark')^ == value]
