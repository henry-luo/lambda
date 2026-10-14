// The shared strict-XML flavor is exercised independently of score normalization.
fn strict(source) => parse(source,{type:'xml',flavor:'strict'})^
fn rejected(source) => (strict(source) or "rejected")=="rejected";
[
 name(strict("<!DOCTYPE a><a/>"))=='document',
 name(strict("<!DOCTYPE a PUBLIC '-//Example//DTD Example 1.0//EN' 'https://invalid.example/dtd'><a/>"))=='document',
 rejected("<!DOCTYPE a PUBLIC 'bad & id' 'unused'><a/>"),
 rejected("<!DOCTYPE a SYSTEM'unseparated'><a/>"),
 rejected("<!DOCTYPE a PUBLIC 'only one literal'><a/>"),
 rejected("<a><!DOCTYPE a></a>"),
 rejected("<!DOCTYPE a [<!ENTITY x SYSTEM 'file:///secret'>]><a>&x;</a>"),
 rejected("<a>&unknown;</a>"),
 rejected("<x:a/>"),
 rejected("<a x='1' x='2'/>"),
 strict("<a id='12'>&lt;safe&gt;</a>")[0].id=="12",
 strict("<a id='12'>&lt;safe&gt;</a>")[0][0]=="<safe>"
]
