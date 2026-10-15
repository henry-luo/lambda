import c: .common

// A closed constructor registry keeps source names out of executable tag positions (S1.8).
let tags = {
    html:(a,k)=> <html *:a,*k>,head:(a,k)=> <head *:a,*k>,body:(a,k)=> <body *:a,*k>,
    main:(a,k)=> <main *:a,*k>,meta:(a,k)=> <meta *:a,*k>,link:(a,k)=> <link *:a,*k>,
    title:(a,k)=> <title *:a,*k>,
    article:(a,k)=> <article *:a,*k>,section:(a,k)=> <section *:a,*k>,div:(a,k)=> <div *:a,*k>,
    p:(a,k)=> <p *:a,*k>,span:(a,k)=> <span *:a,*k>,a:(a,k)=> <a *:a,*k>,
    h1:(a,k)=> <h1 *:a,*k>,h2:(a,k)=> <h2 *:a,*k>,h3:(a,k)=> <h3 *:a,*k>,
    h4:(a,k)=> <h4 *:a,*k>,h5:(a,k)=> <h5 *:a,*k>,h6:(a,k)=> <h6 *:a,*k>,
    em:(a,k)=> <em *:a,*k>,strong:(a,k)=> <strong *:a,*k>,code:(a,k)=> <code *:a,*k>,
    b:(a,k)=> <b *:a,*k>,i:(a,k)=> <i *:a,*k>,s:(a,k)=> <s *:a,*k>,
    sup:(a,k)=> <sup *:a,*k>,sub:(a,k)=> <sub *:a,*k>,q:(a,k)=> <q *:a,*k>,
    kbd:(a,k)=> <kbd *:a,*k>,samp:(a,k)=> <samp *:a,*k>,var:(a,k)=> <var *:a,*k>,
    ul:(a,k)=> <ul *:a,*k>,ol:(a,k)=> <ol *:a,*k>,li:(a,k)=> <li *:a,*k>,
    dl:(a,k)=> <dl *:a,*k>,dt:(a,k)=> <dt *:a,*k>,dd:(a,k)=> <dd *:a,*k>,
    blockquote:(a,k)=> <blockquote *:a,*k>,pre:(a,k)=> <pre *:a,*k>,
    table:(a,k)=> <table *:a,*k>,thead:(a,k)=> <thead *:a,*k>,tbody:(a,k)=> <tbody *:a,*k>,
    tfoot:(a,k)=> <tfoot *:a,*k>,tr:(a,k)=> <tr *:a,*k>,th:(a,k)=> <th *:a,*k>,td:(a,k)=> <td *:a,*k>,
    colgroup:(a,k)=> <colgroup *:a,*k>,col:(a,k)=> <col *:a,*k>,caption:(a,k)=> <caption *:a,*k>,
    img:(a,k)=> <img *:a,*k>,br:(a,k)=> <br *:a,*k>,hr:(a,k)=> <hr *:a,*k>,
    header:(a,k)=> <header *:a,*k>,footer:(a,k)=> <footer *:a,*k>,nav:(a,k)=> <nav *:a,*k>,
    aside:(a,k)=> <aside *:a,*k>,figure:(a,k)=> <figure *:a,*k>,figcaption:(a,k)=> <figcaption *:a,*k>,
    video:(a,k)=> <video *:a,*k>,audio:(a,k)=> <audio *:a,*k>,source:(a,k)=> <source *:a,*k>,
    cite:(a,k)=> <cite *:a,*k>,abbr:(a,k)=> <abbr *:a,*k>
}
pub fn make(tag,attrs,kids) element^ {
    if (tags[tag] == null) raise c.fail("invalid-source","Unknown HTML constructor " ++ tag)
    else tags[tag](attrs,kids)^
}
pub fn attributes(node,classes = [],generated = false) {
    let values = c.classes([*classes,c.attr(node,"role"),
        if (c.attr(node,"revisionflag") == null) null else "rev" ++ c.attr(node,"revisionflag")]);
    {*:if (node.explicit_id or (generated and node.parent != null)) {id:node.id} else {},
        *:if (values == "") {} else {class:values},
        *:if (c.attr(node,"lang",c.XML) == null) {} else {lang:c.attr(node,"lang",c.XML)},
        *:if (c.attr(node,"dir") == null) {} else {dir:c.attr(node,"dir")}}
}
