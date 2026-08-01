"""The sole hand-maintained predefined-name catalog.

The pool number is encoded in the upper 16 bits of NameId; ordinals begin at
one.  Later pools may alias an owning spelling rather than create a second
STRING identity.
"""

MARKUP_TAGS = """
A ABBR ACRONYM ADDRESS ALTGLYPH ALTGLYPHDEF ALTGLYPHITEM ANIMATECOLOR
ANIMATEMOTION ANIMATETRANSFORM ANNOTATION_XML APPLET AREA ARTICLE ASIDE AUDIO
B BASE BASEFONT BDI BDO BGSOUND BIG BLINK BLOCKQUOTE BODY BR BUTTON CANVAS
CAPTION CENTER CITE CLIPPATH CODE COL COLGROUP DATA DATALIST DD DEL DESC DETAILS
DFN DIALOG DIR DIV DL DT EM EMBED FEBLEND FECOLORMATRIX FECOMPONENTTRANSFER
FECOMPOSITE FECONVOLVEMATRIX FEDIFFUSELIGHTING FEDISPLACEMENTMAP FEDISTANTLIGHT
FEDROPSHADOW FEFLOOD FEFUNCA FEFUNCB FEFUNCG FEFUNCR FEGAUSSIANBLUR FEIMAGE
FEMERGE FEMERGENODE FEMORPHOLOGY FEOFFSET FEPOINTLIGHT FESPECULARLIGHTING
FESPOTLIGHT FETILE FETURBULENCE FIELDSET FIGCAPTION FIGURE FONT FOOTER
FOREIGNOBJECT FORM FRAME FRAMESET GLYPHREF H1 H2 H3 H4 H5 H6 HEAD HEADER HGROUP
HR HTML I IFRAME IMAGE IMG INPUT INS ISINDEX KBD KEYGEN LABEL LEGEND LI
LINEARGRADIENT LINK LISTING MAIN MALIGNMARK MAP MARK MARQUEE MATH MENU META
METER MFENCED MGLYPH MI MN MO MS MTEXT MULTICOL NAV NEXTID NOBR NOEMBED NOFRAMES
NOSCRIPT OBJECT OL OPTGROUP OPTION OUTPUT P PARAM PATH PICTURE PLAINTEXT PRE
PROGRESS Q RADIALGRADIENT RB RP RT RTC RUBY S SAMP SCRIPT SECTION SELECT SLOT
SMALL SOURCE SPACER SPAN STRIKE STRONG STYLE SUB SUMMARY SUP SVG TABLE TBODY TD
TEMPLATE TEXTAREA TEXTPATH TFOOT TH THEAD TIME TITLE TR TRACK TT U UL VAR VIDEO
WBR WEBVIEW XMP
""".split()

# This is the only spelling authority for generated markup tag identities. The
# original HTML table was a second authority and allowed native code to assign
# a different identity to a generated spelling.
MARKUP_TAG_ENTRIES = [(tag, tag.lower().replace("_", "-")) for tag in MARKUP_TAGS]
MARKUP_EXTRA_ENTRIES = [
    ("CLASS", "class"), ("ID", "id"), ("HREF", "href"), ("SRC", "src"),
    ("WIDTH", "width"), ("HEIGHT", "height"), ("DISPLAY", "display"),
    ("COLOR", "color"), ("BACKGROUND", "background"), ("OPEN", "open"),
    ("CONTROLS", "controls"),
]
MARKUP_ENTRY_SYMBOLS = {
    spelling: symbol for symbol, spelling in MARKUP_TAG_ENTRIES + MARKUP_EXTRA_ENTRIES
}

# CSS dispatch codes are deliberately defined by css_properties.cpp.  This list
# owns their byte spelling for generated NameId lookup; shared markup spellings
# remain owned by the earlier markup records below.
CSS_PROPERTY_SPELLINGS = """
display width height color background font position top right bottom left z-index float clear overflow overflow-x overflow-y
visibility clip clip-path direction unicode-bidi writing-mode min-width min-height
max-width max-height margin-top margin-right margin-bottom margin-left margin-block
margin-block-start margin-block-end margin-inline margin-inline-start margin-inline-end
margin-trim padding-top padding-right padding-bottom padding-left padding-block
padding-block-start padding-block-end padding-inline padding-inline-start padding-inline-end
border-top-width border-right-width border-bottom-width border-left-width border-top-style
border-right-style border-bottom-style border-left-style border-top-color border-right-color
border-bottom-color border-left-color border-width border-style border-color border-top
border-right border-bottom border-left border-inline border-inline-start border-inline-end
border-block border-block-start border-block-end border-block-width border-block-color
border-block-start-width border-block-start-color border-block-end-color border-block-end-width
box-sizing box-decoration-break aspect-ratio fill stroke stroke-width font-family font-size
font-weight font-style font-variant font-size-adjust font-kerning font-variant-ligatures
font-variant-caps font-variant-numeric font-variant-alternates font-variant-east-asian
font-feature-settings font-language-override font-optical-sizing font-variation-settings
font-display letter-spacing word-spacing text-shadow line-height text-align text-decoration
text-transform initial-letter text-wrap-style text-spacing-trim white-space vertical-align
background-color background-image background-repeat background-position background-size
flex-direction flex-wrap flex-flow justify-content align-items align-content align-self
flex-grow flex-shrink flex-basis order grid-template-columns grid-template-rows
grid-column-start grid-column-end grid-row-start grid-row-end grid-column-gap grid-row-gap
justify-items justify-self place-items place-self grid-template grid-template-areas grid-auto-rows
grid-auto-columns grid-auto-flow grid-row grid-column grid-area grid-gap opacity cursor
border-radius border-top-left-radius border-top-right-radius border-bottom-right-radius
border-bottom-left-radius background-attachment background-origin background-clip
background-position-x background-position-y background-blend-mode filter backdrop-filter
transform transform-origin transform-style backface-visibility perspective perspective-origin
animation animation-name animation-duration animation-timing-function animation-delay
animation-iteration-count animation-direction animation-fill-mode animation-play-state transition
margin padding border flex grid column-width column-count columns column-rule column-rule-width
column-rule-style column-rule-color column-span column-fill column-height column-wrap gap row-gap
column-gap block-size inline-size min-block-size min-inline-size max-block-size max-inline-size
inset inset-block inset-block-start inset-block-end inset-inline inset-inline-start inset-inline-end
text-decoration-line text-decoration-style text-decoration-color text-decoration-thickness
text-emphasis text-emphasis-style text-emphasis-color text-emphasis-position text-overflow word-break
line-break hyphens overflow-wrap word-wrap tab-size hanging-punctuation text-justify text-align-all
text-align-last list-style list-style-type list-style-position list-style-image counter-reset
counter-increment counter-set content quotes font-stretch text-orientation text-combine-upright
text-indent border-collapse border-spacing caption-side empty-cells table-layout resize accent-color
caret-color caret-shape nav-index nav-up nav-right nav-down nav-left appearance user-select
box-shadow border-image border-image-source border-image-slice border-image-width border-image-outset
border-image-repeat outline outline-style outline-width outline-color outline-offset break-before
break-after break-inside page-break-before page-break-after page-break-inside orphans widows container
container-type container-name contain contain-intrinsic-width contain-intrinsic-height
contain-intrinsic-size alignment-baseline baseline-shift baseline-source dominant-baseline text-box
text-box-trim text-box-edge isolation mix-blend-mode object-fit object-position object-view-box
pointer-events float-defer float-offset float-reference image-orientation image-rendering marker-offset
mask-image mask-type nesting overflow-block overflow-clip-margin overflow-inline overscroll-behavior
ruby-align ruby-position scroll-behavior scroll-margin scroll-padding scroll-snap-align
scroll-snap-type transition-delay transition-duration transition-property transition-timing-function
wrap-flow wrap-through line-clamp -webkit-line-clamp
""".split()

CSS_PROPERTY_ENTRIES = [
    ("CSS_" + spelling.lstrip("-").upper().replace("-", "_").replace("*", "STAR"), spelling)
    for spelling in CSS_PROPERTY_SPELLINGS
    if spelling not in MARKUP_ENTRY_SYMBOLS  # markup owns shared spellings.
]

POOLS = {
    0: ("markup", MARKUP_TAG_ENTRIES + MARKUP_EXTRA_ENTRIES + CSS_PROPERTY_ENTRIES),
    1: ("lambda", [
        ("TYPE", "type"), ("STRING", "string"), ("NUMBER", "number"),
        ("BOOLEAN", "boolean"), ("LENGTH", "length"),
    ]),
    2: ("js", [
        ("CONSTRUCTOR", "constructor"), ("PROTOTYPE", "prototype"),
        ("NAME", "name"), ("TO_STRING", "toString"), ("VALUE_OF", "valueOf"),
        ("ITERATOR", "Symbol.iterator", "SYMBOL"),
        ("TO_PRIMITIVE", "Symbol.toPrimitive", "SYMBOL"),
        ("HAS_INSTANCE", "Symbol.hasInstance", "SYMBOL"),
        ("TO_STRING_TAG", "Symbol.toStringTag", "SYMBOL"),
        ("ASYNC_ITERATOR", "Symbol.asyncIterator", "SYMBOL"),
        ("SPECIES", "Symbol.species", "SYMBOL"), ("MATCH", "Symbol.match", "SYMBOL"),
        ("REPLACE", "Symbol.replace", "SYMBOL"), ("SEARCH", "Symbol.search", "SYMBOL"),
        ("SPLIT", "Symbol.split", "SYMBOL"), ("UNSCOPABLES", "Symbol.unscopables", "SYMBOL"),
        ("IS_CONCAT_SPREADABLE", "Symbol.isConcatSpreadable", "SYMBOL"),
        ("MATCH_ALL", "Symbol.matchAll", "SYMBOL"),
        ("ASYNC_DISPOSE", "Symbol.asyncDispose", "SYMBOL"),
        ("DISPOSE", "Symbol.dispose", "SYMBOL"),
    ]),
}
