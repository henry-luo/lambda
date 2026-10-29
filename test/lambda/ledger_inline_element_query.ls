type P = <p>
type Span = <span>
let doc = <div <p "one"> <span "two"> <p "three">>;
[doc[<p>] == doc[P], doc[<span>] == doc[Span], doc[<missing>] == null]
