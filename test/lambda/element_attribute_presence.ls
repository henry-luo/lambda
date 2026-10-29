// S16.9.5: required nullable attributes still require a stored key.
type Required = <e a: int?>
type Optional = <e a?: int>
type NullableOptional = <e a?: int?>;
[<e> is Required, <e a:null> is Required, <e a:2> is Required,
 <e> is Optional, <e a:null> is Optional, <e a:2> is Optional,
 <e> is NullableOptional, <e a:null> is NullableOptional]
