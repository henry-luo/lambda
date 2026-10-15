// D7.2.5: a template-owned stylesheet may affect nodes outside its result subtree.
edit <style_app> state phase:0 {
    let kids = [
        if (phase==3) null else <style id:"live-sheet",
            if (phase==0) "#outside { color:red; background-color:yellow; } #inside { color:red; }"
            else if (phase==1 or phase==2) "#outside, #inside { color:blue; }"
            else "#outside, #inside { color:green; }">,
        <p id:"inside","Inside template">,
        <button id:"toggle","Change stylesheet">]
    // a fragment forces the full rebuild path; element results use subtree replacement.
    if (phase==2 or phase==3) kids else <section id:"style-host",*kids>
}
on click() { phase = (phase+1)%5 }
<html <head <style "#outside { color:#112233; }">>
    <body <p id:"outside","Outside template"> apply(<style_app>,{mode:"edit"})>>
