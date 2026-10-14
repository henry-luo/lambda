fn xml(source,preserve) element^ => parse(source,{type:'xml',flavor:'strict',preserve_whitespace:preserve})^
let source="<a><text>two </text><text> words</text></a>"
let kept=xml(source,true)^
let default=parse(source,{type:'xml',flavor:'strict'})^;
[
 kept[0][0][0]=="two ",
 kept[0][1][0]==" words",
 default[0][0][0]=="two",
 default[0][1][0]=="words",
 xml(source,false)^==default,
 xml("<a>  <b/>  </a>",true)^[0][0]=="  ",
 len(xml("<a>  <b/>  </a>",false)^[0])==1,
 xml("<a><![CDATA[ two ]]></a>",true)^[0][0]==" two ",
 (parse("<a/>",{type:'xml',flavor:'strict',preserve_whitespace:1}) or "invalid")=="invalid",
 (xml("<a>&undeclared;</a>",true) or "invalid")=="invalid"
]
