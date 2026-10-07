// TeX expansion engine: primitive table, main control, assignments,
// typesetting primitives, output emission and reconstruction.
#ifndef LAMBDA_NO_LATEX

#include "input-tex-internal.hpp"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <stdio.h>
#include <new>

namespace tex {

static const uint16_t LEVEL_ONE = 1;
static const int32_t UNITY = 65536;

// ======================================================================
// primitive table
// ======================================================================

struct PrimDef {
    const char* name;
    uint32_t code;
    uint8_t sub;
};

static const PrimDef PRIMS[] = {
    // expandable
    {"expandafter", P_EXPANDAFTER, 0}, {"noexpand", P_NOEXPAND, 0}, {"csname", P_CSNAME, 0},
    {"string", P_STRING, 0}, {"number", P_NUMBER, 0}, {"romannumeral", P_ROMANNUMERAL, 0},
    {"the", P_THE, 0}, {"unexpanded", P_UNEXPANDED, 0}, {"detokenize", P_DETOKENIZE, 0},
    {"meaning", P_MEANING, 0}, {"fontname", P_FONTNAME, 0}, {"jobname", P_JOBNAME, 0},
    {"input", P_INPUT, 0}, {"endinput", P_ENDINPUT, 0}, {"scantokens", P_SCANTOKENS, 0},
    {"expanded", P_EXPANDED, 0}, {"eTeXrevision", P_ETEXREVISION, 0}, {"pdfstrcmp", P_STRCMP, 0},
    {"topmark", P_TOPMARK, 0}, {"firstmark", P_FIRSTMARK, 0}, {"botmark", P_BOTMARK, 0},
    {"splitfirstmark", P_SPLITFIRSTMARK, 0}, {"splitbotmark", P_SPLITBOTMARK, 0},
    {"topmarks", P_TOPMARKS, 0}, {"firstmarks", P_FIRSTMARKS, 0}, {"botmarks", P_BOTMARKS, 0},
    {"splitfirstmarks", P_SPLITFIRSTMARKS, 0}, {"splitbotmarks", P_SPLITBOTMARKS, 0},
    {"pdfuniformdeviate", P_UNIFORMDEVIATE, 0}, {"pdfnormaldeviate", P_NORMALDEVIATE, 0},
    {"pdftexrevision", P_PDFTEXREVISION, 0}, {"pdfescapestring", P_ESCAPESTRING, 0},
    {"pdfescapename", P_ESCAPENAME, 0}, {"pdfescapehex", P_ESCAPEHEX, 0},
    {"pdfunescapehex", P_UNESCAPEHEX, 0}, {"pdfmdfivesum", P_MDFIVESUM, 0},
    {"pdffilesize", P_FILESIZE, 0}, {"pdffilemoddate", P_FILEMODDATE, 0},
    {"pdfcreationdate", P_CREATIONDATE, 0},
    {"if", P_IF, 0}, {"ifcat", P_IFCAT, 0}, {"ifnum", P_IFNUM, 0}, {"ifdim", P_IFDIM, 0},
    {"ifodd", P_IFODD, 0}, {"ifvmode", P_IFVMODE, 0}, {"ifhmode", P_IFHMODE, 0},
    {"ifmmode", P_IFMMODE, 0}, {"ifinner", P_IFINNER, 0}, {"ifvoid", P_IFVOID, 0},
    {"ifhbox", P_IFHBOX, 0}, {"ifvbox", P_IFVBOX, 0}, {"ifx", P_IFX, 0}, {"ifeof", P_IFEOF, 0},
    {"iftrue", P_IFTRUE, 0}, {"iffalse", P_IFFALSE, 0}, {"ifcase", P_IFCASE, 0},
    {"ifdefined", P_IFDEFINED, 0}, {"ifcsname", P_IFCSNAME, 0}, {"iffontchar", P_IFFONTCHAR, 0},
    {"ifpdfprimitive", P_IFPDFPRIMITIVE, 0}, {"ifincsname", P_IFINCSNAME, 0},
    {"unless", P_UNLESS, 0}, {"fi", P_FI, 0}, {"else", P_ELSE, 0}, {"or", P_OR, 0},
    // general
    {"relax", P_RELAX, 0}, {"endcsname", P_ENDCSNAME, 0}, {"def", P_DEF, 0}, {"gdef", P_GDEF, 0},
    {"edef", P_EDEF, 0}, {"xdef", P_XDEF, 0}, {"let", P_LET, 0}, {"futurelet", P_FUTURELET, 0},
    {"global", P_GLOBAL, 0}, {"long", P_LONG, 0}, {"outer", P_OUTER, 0},
    {"protected", P_PROTECTED, 0}, {"chardef", P_CHARDEF, 0}, {"mathchardef", P_MATHCHARDEF, 0},
    {"countdef", P_COUNTDEF, 0}, {"dimendef", P_DIMENDEF, 0}, {"skipdef", P_SKIPDEF, 0},
    {"muskipdef", P_MUSKIPDEF, 0}, {"toksdef", P_TOKSDEF, 0}, {"count", P_COUNT, 0},
    {"dimen", P_DIMEN, 0}, {"skip", P_SKIP, 0}, {"muskip", P_MUSKIP, 0}, {"toks", P_TOKS, 0},
    {"advance", P_ADVANCE, 0}, {"multiply", P_MULTIPLY, 0}, {"divide", P_DIVIDE, 0},
    {"catcode", P_CATCODE, 0}, {"lccode", P_LCCODE, 0}, {"uccode", P_UCCODE, 0},
    {"sfcode", P_SFCODE, 0}, {"mathcode", P_MATHCODE, 0}, {"delcode", P_DELCODE, 0},
    {"numexpr", P_NUMEXPR, 0}, {"dimexpr", P_DIMEXPR, 0}, {"glueexpr", P_GLUEEXPR, 0},
    {"muexpr", P_MUEXPR, 0}, {"gluestretch", P_GLUESTRETCH, 0}, {"glueshrink", P_GLUESHRINK, 0},
    {"gluestretchorder", P_GLUESTRETCHORDER, 0}, {"glueshrinkorder", P_GLUESHRINKORDER, 0},
    {"gluetomu", P_GLUETOMU, 0}, {"mutoglue", P_MUTOGLUE, 0}, {"fontcharwd", P_FONTCHARWD, 0},
    {"fontcharht", P_FONTCHARHT, 0}, {"fontchardp", P_FONTCHARDP, 0},
    {"fontcharic", P_FONTCHARIC, 0}, {"begingroup", P_BEGINGROUP, 0}, {"endgroup", P_ENDGROUP, 0},
    {"aftergroup", P_AFTERGROUP, 0}, {"afterassignment", P_AFTERASSIGNMENT, 0},
    {"ignorespaces", P_IGNORESPACES, 0}, {"uppercase", P_UPPERCASE, 0},
    {"lowercase", P_LOWERCASE, 0}, {"message", P_MESSAGE, 0}, {"errmessage", P_ERRMESSAGE, 0},
    {"show", P_SHOW, 0}, {"showthe", P_SHOWTHE, 0}, {"showbox", P_SHOWBOX, 0},
    {"showlists", P_SHOWLISTS, 0}, {"showtokens", P_SHOWTOKENS, 0},
    {"showgroups", P_SHOWGROUPS, 0}, {"showifs", P_SHOWIFS, 0}, {"immediate", P_IMMEDIATE, 0},
    {"write", P_WRITE, 0}, {"openout", P_OPENOUT, 0}, {"closeout", P_CLOSEOUT, 0},
    {"openin", P_OPENIN, 0}, {"closein", P_CLOSEIN, 0}, {"read", P_READ, 0},
    {"readline", P_READLINE, 0}, {"special", P_SPECIAL, 0}, {"errorstopmode", P_INTERACTION, 3},
    {"scrollmode", P_INTERACTION, 2}, {"nonstopmode", P_INTERACTION, 1},
    {"batchmode", P_INTERACTION, 0}, {"interactionmode", P_INTERACTIONMODE, 0},
    {"end", P_END, 0}, {"dump", P_DUMP, 0}, {"font", P_FONT, 0}, {"nullfont", P_NULLFONT, 0},
    {"fontdimen", P_FONTDIMEN, 0}, {"hyphenchar", P_HYPHENCHAR, 0}, {"skewchar", P_SKEWCHAR, 0},
    {"textfont", P_TEXTFONT, 0}, {"scriptfont", P_SCRIPTFONT, 0},
    {"scriptscriptfont", P_SCRIPTSCRIPTFONT, 0}, {"patterns", P_PATTERNS, 0},
    {"hyphenation", P_HYPHENATION, 0}, {"parshape", P_PARSHAPE, 0},
    {"interlinepenalties", P_INTERLINEPENALTIES, 0}, {"clubpenalties", P_CLUBPENALTIES, 0},
    {"widowpenalties", P_WIDOWPENALTIES, 0}, {"displaywidowpenalties", P_DISPLAYWIDOWPENALTIES, 0},
    {"letterspacefont", P_LETTERSPACEFONT, 0}, {"setbox", P_SETBOX, 0}, {"wd", P_WD, 0},
    {"ht", P_HT, 0}, {"dp", P_DP, 0}, {"pagegoal", P_PAGEDIMEN, 0}, {"pagetotal", P_PAGEDIMEN, 1},
    {"pagestretch", P_PAGEDIMEN, 2}, {"pagefilstretch", P_PAGEDIMEN, 3},
    {"pagefillstretch", P_PAGEDIMEN, 4}, {"pagefilllstretch", P_PAGEDIMEN, 5},
    {"pageshrink", P_PAGEDIMEN, 6}, {"pagedepth", P_PAGEDIMEN, 7},
    {"pdfliteral", P_PDFSTRING_DROP, 1}, {"pdfinfo", P_PDFSTRING_DROP, 1},
    {"pdfcatalog", P_PDFSTRING_DROP, 1}, {"pdfnames", P_PDFSTRING_DROP, 1},
    {"pdftrailer", P_PDFSTRING_DROP, 1}, {"pdfmapfile", P_PDFSTRING_DROP, 1},
    {"pdfmapline", P_PDFSTRING_DROP, 1}, {"pdfglyphtounicode", P_PDFSTRING_DROP, 2},
    {"pdfobj", P_PDFSTRING_DROP, 1}, {"pdfannot", P_PDFSTRING_DROP, 1},
    {"pdfstartlink", P_PDFSTRING_DROP, 1}, {"pdfendlink", P_PDFSTRING_DROP, 0},
    {"pdfsavepos", P_PDFSTRING_DROP, 0}, {"pdfresettimer", P_PDFRESETTIMER, 0},
    {"pdfsetrandomseed", P_SETRANDOMSEED, 0},
    // read-only and aux quantities
    {"lastpenalty", P_READONLY_INT, RO_LAST_PENALTY}, {"lastkern", P_READONLY_INT, RO_LAST_KERN},
    {"badness", P_READONLY_INT, RO_BADNESS}, {"inputlineno", P_READONLY_INT, RO_INPUT_LINE_NO},
    {"eTeXversion", P_READONLY_INT, RO_ETEX_VERSION},
    {"currentgrouplevel", P_READONLY_INT, RO_CURRENT_GROUP_LEVEL},
    {"currentgrouptype", P_READONLY_INT, RO_CURRENT_GROUP_TYPE},
    {"currentiflevel", P_READONLY_INT, RO_CURRENT_IF_LEVEL},
    {"currentiftype", P_READONLY_INT, RO_CURRENT_IF_TYPE},
    {"currentifbranch", P_READONLY_INT, RO_CURRENT_IF_BRANCH},
    {"lastnodetype", P_READONLY_INT, RO_LAST_NODE_TYPE},
    {"pdftexversion", P_READONLY_INT, RO_PDFTEX_VERSION},
    {"pdfelapsedtime", P_READONLY_INT, RO_ELAPSED_TIME},
    {"pdfrandomseed", P_READONLY_INT, RO_RANDOM_SEED},
    {"spacefactor", P_READONLY_INT, RO_SPACE_FACTOR}, {"prevgraf", P_READONLY_INT, RO_PREV_GRAF},
    {"deadcycles", P_READONLY_INT, RO_DEAD_CYCLES},
    {"insertpenalties", P_READONLY_INT, RO_INSERT_PENALTIES},
    {"prevdepth", P_READONLY_INT, RO_PREV_DEPTH}, {"lastskip", P_READONLY_INT, RO_LAST_SKIP},
    {"parshapelength", P_READONLY_INT, RO_PARSHAPE_LENGTH},
    {"parshapeindent", P_READONLY_INT, RO_PARSHAPE_INDENT},
    {"parshapedimen", P_READONLY_INT, RO_PARSHAPE_DIMEN},
    {"pdflastxpos", P_READONLY_INT, RO_LAST_X_POS}, {"pdflastypos", P_READONLY_INT, RO_LAST_Y_POS},
    {"pdfshellescape", P_READONLY_INT, RO_SHELL_ESCAPE},
    // typesetting
    {"par", P_PAR, 0}, {"indent", P_INDENT, 0}, {"noindent", P_NOINDENT, 0},
    {"hskip", P_HSKIP, 0}, {"vskip", P_VSKIP, 0}, {"hfil", P_HFIL, 0}, {"hfill", P_HFILL, 0},
    {"hss", P_HSS, 0}, {"hfilneg", P_HFILNEG, 0}, {"vfil", P_VFIL, 0}, {"vfill", P_VFILL, 0},
    {"vss", P_VSS, 0}, {"vfilneg", P_VFILNEG, 0}, {"kern", P_KERN, 0}, {"mkern", P_MKERN, 0},
    {"mskip", P_MSKIP, 0}, {"penalty", P_PENALTY, 0}, {"unpenalty", P_UNPENALTY, 0},
    {"unkern", P_UNKERN, 0}, {"unskip", P_UNSKIP, 0}, {"hbox", P_HBOX, 0}, {"vbox", P_VBOX, 0},
    {"vtop", P_VTOP, 0}, {"box", P_BOX, 0}, {"copy", P_COPY, 0}, {"lastbox", P_LASTBOX, 0},
    {"vsplit", P_VSPLIT, 0}, {"unhbox", P_UNHBOX, 0}, {"unhcopy", P_UNHCOPY, 0},
    {"unvbox", P_UNVBOX, 0}, {"unvcopy", P_UNVCOPY, 0}, {"raise", P_RAISE, 0},
    {"lower", P_LOWER, 0}, {"moveleft", P_MOVELEFT, 0}, {"moveright", P_MOVERIGHT, 0},
    {"hrule", P_HRULE, 0}, {"vrule", P_VRULE, 0}, {"leaders", P_LEADERS, 0},
    {"cleaders", P_CLEADERS, 0}, {"xleaders", P_XLEADERS, 0}, {"mark", P_MARK, 0},
    {"marks", P_MARKS, 0}, {"insert", P_INSERT, 0}, {"vadjust", P_VADJUST, 0},
    {"halign", P_HALIGN, 0}, {"valign", P_VALIGN, 0}, {"noalign", P_NOALIGN, 0},
    {"omit", P_OMIT, 0}, {"span", P_SPAN, 0}, {"cr", P_CR, 0}, {"crcr", P_CRCR, 0},
    {"char", P_CHAR, 0}, {"accent", P_ACCENT, 0}, {"discretionary", P_DISCRETIONARY, 0},
    {"-", P_DISC_HYPHEN, 0}, {"/", P_ITAL_CORR, 0}, {" ", P_CONTROL_SPACE, 0},
    {"shipout", P_SHIPOUT, 0}, {"noboundary", P_NOBOUNDARY, 0},
    {"pagediscards", P_PAGEDISCARDS, 0}, {"splitdiscards", P_PAGEDISCARDS, 1},
    {"vcenter", P_VCENTER, 0},
    {"mathord", P_MATH_EMIT, 0}, {"mathop", P_MATH_EMIT, 1}, {"mathbin", P_MATH_EMIT, 2},
    {"mathrel", P_MATH_EMIT, 3}, {"mathopen", P_MATH_EMIT, 4}, {"mathclose", P_MATH_EMIT, 5},
    {"mathpunct", P_MATH_EMIT, 6}, {"mathinner", P_MATH_EMIT, 7},
    {"underline", P_MATH_EMIT, 8}, {"overline", P_MATH_EMIT, 9},
    {"displaystyle", P_MATH_EMIT, 10}, {"textstyle", P_MATH_EMIT, 11},
    {"scriptstyle", P_MATH_EMIT, 12}, {"scriptscriptstyle", P_MATH_EMIT, 13},
    {"limits", P_MATH_EMIT, 14}, {"nolimits", P_MATH_EMIT, 15},
    {"displaylimits", P_MATH_EMIT, 16}, {"nonscript", P_MATH_EMIT, 17},
    {"eqno", P_MATH_EMIT, 18}, {"leqno", P_MATH_EMIT, 19}, {"mathchoice", P_MATH_EMIT, 20},
    {"left", P_MATH_EMIT, 21}, {"right", P_MATH_EMIT, 22}, {"middle", P_MATH_EMIT, 23},
    {"over", P_MATH_EMIT, 24}, {"atop", P_MATH_EMIT, 25}, {"above", P_MATH_EMIT, 26},
    {"overwithdelims", P_MATH_EMIT, 27}, {"atopwithdelims", P_MATH_EMIT, 28},
    {"abovewithdelims", P_MATH_EMIT, 29},
    {"mathchar", P_MATH_DELIM_EMIT, 0}, {"delimiter", P_MATH_DELIM_EMIT, 1},
    {"radical", P_MATH_DELIM_EMIT, 2}, {"mathaccent", P_MATH_DELIM_EMIT, 3},
};

static const char* INT_PAR_NAMES[IP_COUNT] = {
    "pretolerance", "tolerance", "linepenalty", "hyphenpenalty", "exhyphenpenalty",
    "clubpenalty", "widowpenalty", "displaywidowpenalty", "brokenpenalty", "binoppenalty",
    "relpenalty", "predisplaypenalty", "postdisplaypenalty", "interlinepenalty",
    "doublehyphendemerits", "finalhyphendemerits", "adjdemerits", "mag", "delimiterfactor",
    "looseness", "time", "day", "month", "year", "showboxbreadth", "showboxdepth", "hbadness",
    "vbadness", "pausing", "tracingonline", "tracingmacros", "tracingstats", "tracingparagraphs",
    "tracingpages", "tracingoutput", "tracinglostchars", "tracingcommands", "tracingrestores",
    "uchyph", "outputpenalty", "maxdeadcycles", "hangafter", "floatingpenalty", "globaldefs",
    "fam", "escapechar", "defaulthyphenchar", "defaultskewchar", "endlinechar", "newlinechar",
    "language", "lefthyphenmin", "righthyphenmin", "holdinginserts", "errorcontextlines",
    "tracingassigns", "tracinggroups", "tracingifs", "tracingscantokens", "tracingnesting",
    "predisplaydirection", "lastlinefit", "savingvdiscards", "savinghyphcodes", "TeXXeTstate",
    "pdfoutput", "pdfcompresslevel", "pdfdecimaldigits", "pdfminorversion",
    "pdfobjcompresslevel", "pdfpkresolution", "pdfdraftmode", "pdfgentounicode",
    "pdfadjustspacing", "pdfprotrudechars", "pdftracingfonts", "pdfimageresolution",
    "pdfinclusionerrorlevel",
};

static const char* DIMEN_PAR_NAMES[DP_COUNT] = {
    "parindent", "mathsurround", "lineskiplimit", "hsize", "vsize", "maxdepth", "splitmaxdepth",
    "boxmaxdepth", "hfuzz", "vfuzz", "delimitershortfall", "nulldelimiterspace", "scriptspace",
    "predisplaysize", "displaywidth", "displayindent", "overfullrule", "hangindent", "hoffset",
    "voffset", "emergencystretch", "pdfpagewidth", "pdfpageheight", "pdfhorigin", "pdfvorigin",
    "pdflinkmargin", "pdfdestmargin", "pdfthreadmargin", "pdfpxdimen",
};

static const char* GLUE_PAR_NAMES[GP_COUNT] = {
    "lineskip", "baselineskip", "parskip", "abovedisplayskip", "belowdisplayskip",
    "abovedisplayshortskip", "belowdisplayshortskip", "leftskip", "rightskip", "topskip",
    "splittopskip", "tabskip", "spaceskip", "xspaceskip", "parfillskip", "thinmuskip",
    "medmuskip", "thickmuskip",
};

static const char* TOKS_PAR_NAMES[TP_COUNT] = {
    "output", "everypar", "everymath", "everydisplay", "everyhbox", "everyvbox", "everyjob",
    "everycr", "errhelp", "everyeof", "pdfpagesattr", "pdfpageattr", "pdfpageresources",
    "pdfpkmode",
};

void init_primitives(Engine* e) {
    for (size_t i = 0; i < sizeof(PRIMS) / sizeof(PRIMS[0]); i++)
        define_primitive(e, PRIMS[i].name, PRIMS[i].code, PRIMS[i].sub);
    for (uint32_t i = 0; i < IP_COUNT; i++) define_primitive(e, INT_PAR_NAMES[i], P_INT_PARAM, (uint8_t)i);
    for (uint32_t i = 0; i < DP_COUNT; i++) define_primitive(e, DIMEN_PAR_NAMES[i], P_DIMEN_PARAM, (uint8_t)i);
    for (uint32_t i = 0; i < GP_COUNT; i++) {
        bool mu = i >= GP_THIN_MU_SKIP;
        define_primitive(e, GLUE_PAR_NAMES[i], mu ? P_MU_GLUE_PARAM : P_GLUE_PARAM, (uint8_t)i);
    }
    for (uint32_t i = 0; i < TP_COUNT; i++) define_primitive(e, TOKS_PAR_NAMES[i], P_TOKS_PARAM, (uint8_t)i);
}

// ======================================================================
// output
// ======================================================================

void emit_token(Engine* e, const Token& t) {
    OutItem it = {};
    it.kind = OUT_TOKEN;
    it.tok = t;
    it.tok.flags &= (uint16_t)~TF_NOEXPAND;
    it.origin = e->last_main_offset;
    e->out.append(it);
}

void emit_text(Engine* e, const char* text, size_t len) {
    if (len == 0) return;
    OutItem it = {};
    it.kind = OUT_TEXT;
    it.text = arena_copy(e, text, len);
    it.text_len = (uint32_t)len;
    it.origin = e->last_main_offset;
    e->out.append(it);
}

void emit_span(Engine* e, int32_t file, uint32_t start, uint32_t end, uint32_t first_seq, uint32_t last_seq) {
    if (end <= start) return;
    OutItem it = {};
    it.kind = OUT_SPAN;
    it.file = file;
    it.start = start;
    it.end = end;
    it.tok.seq = first_seq;
    it.last_seq = last_seq;
    it.origin = file == e->main_file ? start : e->last_main_offset;
    e->out.append(it);
}

void emit_passthrough(Engine* e, const Token& t, uint32_t name_id) {
    if (t.kind == TK_CS && t.value == name_id) {
        emit_token(e, t);
        return;
    }
    Token s = make_cs(name_id);
    emit_token(e, s);
}

void emit_canonical_cs(Engine* e, const char* name) {
    Token s = make_cs(intern_cstr(e, name));
    emit_token(e, s);
}

EchoMark echo_begin(Engine* e, const Token& t) {
    EchoMark m = {};
    m.valid = (t.flags & TF_DIRECT) && t.file == e->main_file && t.kind == TK_CS;
    m.start = t.start;
    m.first_seq = t.seq;
    m.foreign_reads = e->foreign_reads;
    m.saved_min = e->echo_min_seq;
    m.saved_max = e->echo_max_seq;
    e->echo_min_seq = UINT32_MAX;
    e->echo_max_seq = 0;
    return m;
}

void echo_cancel(Engine* e, const EchoMark& m) {
    uint32_t lo = e->echo_min_seq;
    uint32_t hi = e->echo_max_seq;
    e->echo_min_seq = lo < m.saved_min ? lo : m.saved_min;
    e->echo_max_seq = hi > m.saved_max ? hi : m.saved_max;
}

bool echo_finish(Engine* e, const EchoMark& m) {
    uint32_t lo = e->echo_min_seq;
    uint32_t hi = e->echo_max_seq;
    e->echo_min_seq = lo < m.saved_min ? lo : m.saved_min;
    e->echo_max_seq = hi > m.saved_max ? hi : m.saved_max;
    if (!m.valid || e->foreign_reads != m.foreign_reads) return false;
    if (lo != UINT32_MAX && lo < m.first_seq) return false;
    uint32_t end_seq = hi > m.first_seq ? hi : m.first_seq;
    // tokens backed up by look-ahead were not consumed by the command
    for (size_t i = e->input.size(); i > 0; i--) {
        const InputLevel& lv = e->input[i - 1];
        if (lv.kind == IN_FILE) {
            if (lv.reader != e->main_reader) return false;
            break;
        }
        for (uint32_t k = lv.index; k < lv.count; k++) {
            const Token& p = lv.toks[k];
            if ((p.flags & TF_DIRECT) && p.file == e->main_file && p.seq <= end_seq && p.seq > 0)
                end_seq = p.seq - 1;
        }
    }
    if (end_seq < m.first_seq) return false;
    if (e->recent_seq[end_seq & 63] != end_seq) return false;
    emit_span(e, e->main_file, m.start, e->recent_end[end_seq & 63], m.first_seq, end_seq);
    return true;
}

// ======================================================================
// groups, braces and math shifts in the main loop
// ======================================================================

static void finish_box(Engine* e) {
    GroupRecord g = e->groups.back();
    if (g.box_target != -1) {
        uint32_t n = (uint32_t)e->out.size() - g.out_start;
        Box* box = nullptr;
        if (g.box_target >= 0) {
            box = (Box*)arena_calloc(e->arena, sizeof(Box));
            box->kind = g.box_kind;
            box->count = n;
            if (n) {
                box->items = (OutItem*)arena_alloc(e->arena, sizeof(OutItem) * n);
                memcpy(box->items, e->out.data() + g.out_start, sizeof(OutItem) * n);
            }
        }
        if (n) e->out.remove_range(g.out_start, n);
        unsave(e);
        if (box) {
            RegValue v = {};
            v.b = box;
            set_reg(e, RK_BOX, (uint32_t)g.box_target, v, g.box_global);
        }
        return;
    }
    unsave(e);
    if (g.emit_close) emit_text(e, "}", 1);
}

void handle_right_brace(Engine* e, const Token& t) {
    if (e->groups.empty()) {
        diag(e, "tex-extra-brace", "Too many }'s");
        emit_token(e, t);
        return;
    }
    switch (e->groups.back().type) {
    case GT_SIMPLE:
        emit_token(e, t);
        unsave(e);
        return;
    case GT_HBOX: case GT_ADJUSTED_HBOX: case GT_VBOX: case GT_VTOP: case GT_INSERT:
    case GT_VCENTER: case GT_OUTPUT:
        finish_box(e);
        return;
    default:
        diag(e, "tex-extra-brace", "Extra }, or forgotten \\endgroup");
        emit_token(e, t);
        return;
    }
}

static void math_shift(Engine* e, const Token& emitted) {
    bool closing = !e->groups.empty() && e->groups.back().type == GT_MATH_SHIFT;
    emit_token(e, emitted);
    if (closing) {
        bool display = e->groups.back().box_kind == 1;
        if (display) {
            Token n;
            if (get_token(e, &n)) {
                Meaning mn = token_meaning(e, n);
                bool shift = (n.kind == TK_CHAR && n.cat == CAT_MATH) || (mn.type == MT_CHAR && mn.cat == CAT_MATH);
                if (shift) emit_token(e, n);
                else {
                    diag(e, "tex-display-math", "Display math should end with $$");
                    back_input(e, n);
                }
            }
        }
        unsave(e);
        if (e->math_depth) e->math_depth--;
        e->display_math = false;
        return;
    }
    bool display = false;
    if (e->math_depth == 0) {
        Token n;
        if (get_token(e, &n)) {
            Meaning mn = token_meaning(e, n);
            if ((n.kind == TK_CHAR && n.cat == CAT_MATH) || (mn.type == MT_CHAR && mn.cat == CAT_MATH)) {
                display = true;
                emit_token(e, n);
            } else {
                back_input(e, n);
            }
        }
    }
    new_save_level(e, GT_MATH_SHIFT);
    e->groups.back().box_kind = display ? 1 : 0;
    e->math_depth++;
    e->display_math = display;
    TokSpan every = e->toks_pars[display ? TP_EVERY_DISPLAY : TP_EVERY_MATH].v;
    if (every.count) back_list(e, every);
}

void handle_char(Engine* e, const Token& t, uint32_t code, uint8_t cat) {
    Token out = t;
    if (t.kind != TK_CHAR) {
        out = make_char(code, cat);
    }
    switch (cat) {
    case CAT_BEGIN:
        new_save_level(e, GT_SIMPLE);
        emit_token(e, out);
        return;
    case CAT_END:
        handle_right_brace(e, out);
        return;
    case CAT_MATH:
        math_shift(e, out);
        return;
    default:
        emit_token(e, out);
        return;
    }
}

// ======================================================================
// assignments (TeX §§1208-1280)
// ======================================================================

enum { PF_LONG = 1, PF_OUTER = 2, PF_GLOBAL = 4, PF_PROTECTED = 8 };

uint32_t get_r_token(Engine* e) {
    Token t;
    for (;;) {
        if (!get_token(e, &t)) return intern_cstr(e, "\x02inaccessible");
        if (t.kind == TK_CHAR && t.cat == CAT_SPACE) continue;
        break;
    }
    if (t.kind != TK_CS) {
        diag(e, "tex-missing-cs", "Missing control sequence inserted");
        back_input(e, t);
        return intern_cstr(e, "\x02inaccessible");
    }
    return t.value;
}

static bool is_assignment(Engine* e, const Meaning& m) {
    if (m.type == MT_REGDEF || m.type == MT_FONT) return true;
    if (m.type != MT_PRIM) return false;
    switch (m.value) {
    case P_DEF: case P_GDEF: case P_EDEF: case P_XDEF: case P_LET: case P_FUTURELET:
    case P_GLOBAL: case P_LONG: case P_OUTER: case P_PROTECTED: case P_CHARDEF: case P_MATHCHARDEF:
    case P_COUNTDEF: case P_DIMENDEF: case P_SKIPDEF: case P_MUSKIPDEF: case P_TOKSDEF:
    case P_COUNT: case P_DIMEN: case P_SKIP: case P_MUSKIP: case P_TOKS: case P_ADVANCE:
    case P_MULTIPLY: case P_DIVIDE: case P_CATCODE: case P_LCCODE: case P_UCCODE: case P_SFCODE:
    case P_MATHCODE: case P_DELCODE: case P_INT_PARAM: case P_DIMEN_PARAM: case P_GLUE_PARAM:
    case P_MU_GLUE_PARAM: case P_TOKS_PARAM: case P_FONT: case P_NULLFONT: case P_FONTDIMEN:
    case P_HYPHENCHAR: case P_SKEWCHAR: case P_TEXTFONT: case P_SCRIPTFONT: case P_SCRIPTSCRIPTFONT:
    case P_SETBOX: case P_PARSHAPE: case P_INTERLINEPENALTIES: case P_CLUBPENALTIES:
    case P_WIDOWPENALTIES: case P_DISPLAYWIDOWPENALTIES: case P_READ: case P_READLINE:
    case P_WD: case P_HT: case P_DP: case P_PAGEDIMEN: case P_INTERACTIONMODE: case P_PATTERNS:
    case P_HYPHENATION: case P_LETTERSPACEFONT: case P_READONLY_INT:
        return true;
    default:
        return false;
    }
    (void)e;
}

static void define_macro_cs(Engine* e, uint32_t cs, uint8_t flags, bool xpand, bool global) {
    lam::ArrayList<Token> params(MEM_CAT_INPUT_OTHER, 8);
    uint8_t nargs = 0;
    // TeX §1218: the cs is not undefined while its definition is scanned
    TokSpan body = scan_toks(e, true, xpand, &params, &nargs);
    Meaning m = {};
    m.type = MT_MACRO;
    m.macro = make_macro(e, freeze(e, params), body, nargs, flags);
    define_cs(e, cs, m, global);
}

static Meaning meaning_for_let(Engine* e, const Token& t) {
    if (t.kind == TK_CHAR) {
        Meaning m = {};
        m.type = MT_CHAR;
        m.value = t.value;
        m.cat = t.cat;
        return m;
    }
    return meaning_of(e, t.value);
}

// register/parameter target of an assignment or \advance
struct Target {
    int level;          // LV_INT .. LV_TOK
    uint8_t kind;       // 0 register, 1 int par, 2 dimen par, 3 glue par, 4 toks par, 5 code, 6 ignored
    uint8_t reg_kind;
    uint32_t index;
    uint8_t code_table;
};

static bool target_of(Engine* e, const Token& t, const Meaning& m, Target* tg) {
    *tg = Target{};
    if (m.type == MT_REGDEF) {
        tg->kind = 0;
        tg->reg_kind = m.sub;
        tg->index = m.value;
    } else if (m.type == MT_PRIM) {
        switch (m.value) {
        case P_COUNT: tg->reg_kind = RK_COUNT; tg->index = (uint32_t)scan_register_num(e); break;
        case P_DIMEN: tg->reg_kind = RK_DIMEN; tg->index = (uint32_t)scan_register_num(e); break;
        case P_SKIP: tg->reg_kind = RK_SKIP; tg->index = (uint32_t)scan_register_num(e); break;
        case P_MUSKIP: tg->reg_kind = RK_MUSKIP; tg->index = (uint32_t)scan_register_num(e); break;
        case P_TOKS: tg->reg_kind = RK_TOKS; tg->index = (uint32_t)scan_register_num(e); break;
        case P_INT_PARAM: tg->kind = 1; tg->index = m.sub; tg->level = LV_INT; return true;
        case P_DIMEN_PARAM: tg->kind = 2; tg->index = m.sub; tg->level = LV_DIMEN; return true;
        case P_GLUE_PARAM: tg->kind = 3; tg->index = m.sub; tg->level = LV_GLUE; return true;
        case P_MU_GLUE_PARAM: tg->kind = 3; tg->index = m.sub; tg->level = LV_MU; return true;
        case P_TOKS_PARAM: tg->kind = 4; tg->index = m.sub; tg->level = LV_TOK; return true;
        default: (void)t; return false;
        }
        tg->kind = 0;
    } else {
        return false;
    }
    tg->level = tg->reg_kind == RK_COUNT ? LV_INT : tg->reg_kind == RK_DIMEN ? LV_DIMEN
        : tg->reg_kind == RK_SKIP ? LV_GLUE : tg->reg_kind == RK_MUSKIP ? LV_MU : LV_TOK;
    return true;
}

static int32_t target_int(Engine* e, const Target& tg) {
    if (tg.kind == 1) return e->int_pars[tg.index].v;
    if (tg.kind == 2) return e->dimen_pars[tg.index].v;
    return reg_slot(e, tg.reg_kind, tg.index)->v.i;
}

static const Glue* target_glue(Engine* e, const Target& tg) {
    const Glue* g = tg.kind == 3 ? e->glue_pars[tg.index].v : reg_slot(e, tg.reg_kind, tg.index)->v.g;
    return g ? g : &ZERO_GLUE;
}

static void store_target(Engine* e, const Target& tg, int32_t i, const Glue* g, TokSpan t, bool global) {
    switch (tg.kind) {
    case 0: {
        RegValue v = {};
        if (tg.level <= LV_DIMEN) v.i = i;
        else if (tg.level == LV_TOK) v.t = t;
        else v.g = g;
        set_reg(e, tg.reg_kind, tg.index, v, global);
        return;
    }
    case 1: set_int_par(e, tg.index, i, global); return;
    case 2: set_dimen_par(e, tg.index, i, global); return;
    case 3: set_glue_par(e, tg.index, g, global); return;
    case 4: set_toks_par(e, tg.index, t, global); return;
    default: return;
    }
}

static void assign_toks(Engine* e, const Target& tg, bool global) {
    scan_optional_equals(e);
    Token t;
    skip_spaces_and_relax(e, &t);
    Meaning m = token_meaning(e, t);
    bool brace = (t.kind == TK_CHAR && t.cat == CAT_BEGIN) || (m.type == MT_CHAR && m.cat == CAT_BEGIN);
    if (!brace && internal_level(e, m) == LV_TOK) {
        Quantity q = scan_something_internal(e, t, m, LV_TOK, false);
        store_target(e, tg, 0, nullptr, q.t, global);
        return;
    }
    back_input(e, t);
    TokSpan list = scan_toks(e, false, false, nullptr, nullptr);
    store_target(e, tg, 0, nullptr, list, global);
}

static void assign_value(Engine* e, const Target& tg, bool global) {
    if (tg.level == LV_TOK) {
        assign_toks(e, tg, global);
        return;
    }
    scan_optional_equals(e);
    if (tg.level == LV_INT) store_target(e, tg, scan_int(e), nullptr, TokSpan{}, global);
    else if (tg.level == LV_DIMEN) store_target(e, tg, scan_dimen(e, false, false, false, 0), nullptr, TokSpan{}, global);
    else store_target(e, tg, 0, scan_glue(e, tg.level == LV_MU), TokSpan{}, global);
}

static int32_t x_over_n(Engine* e, int32_t x, int32_t n) {
    if (n == 0) { e->arith_error = true; return 0; }
    return x / n;
}

static void do_register_command(Engine* e, uint32_t code, bool global) {
    Token t;
    if (!get_x_token(e, &t)) return;
    Meaning m = token_meaning(e, t);
    Target tg;
    if (!target_of(e, t, m, &tg) || tg.level == LV_TOK) {
        diag(e, "tex-cant-use", "You can't use that after \\advance, \\multiply or \\divide");
        back_input(e, t);
        return;
    }
    scan_keyword(e, "by");
    e->arith_error = false;
    int32_t i = 0;
    const Glue* g = nullptr;
    if (code == P_ADVANCE) {
        if (tg.level == LV_INT) i = target_int(e, tg) + scan_int(e);
        else if (tg.level == LV_DIMEN) i = target_int(e, tg) + scan_dimen(e, false, false, false, 0);
        else {
            Glue q = *scan_glue(e, tg.level == LV_MU);
            const Glue* r = target_glue(e, tg);
            q.width += r->width;
            if (q.stretch == 0) q.stretch_order = 0;
            if (q.stretch_order == r->stretch_order) q.stretch += r->stretch;
            else if (q.stretch_order < r->stretch_order && r->stretch != 0) {
                q.stretch = r->stretch;
                q.stretch_order = r->stretch_order;
            }
            if (q.shrink == 0) q.shrink_order = 0;
            if (q.shrink_order == r->shrink_order) q.shrink += r->shrink;
            else if (q.shrink_order < r->shrink_order && r->shrink != 0) {
                q.shrink = r->shrink;
                q.shrink_order = r->shrink_order;
            }
            g = new_glue(e, q.width, q.stretch, q.stretch_order, q.shrink, q.shrink_order);
        }
    } else {
        int32_t n = scan_int(e);
        if (tg.level <= LV_DIMEN) {
            int32_t x = target_int(e, tg);
            if (code == P_MULTIPLY) {
                int64_t prod = (int64_t)x * n;
                int64_t limit = tg.level == LV_INT ? 2147483647LL : 1073741823LL;
                if (prod > limit || prod < -limit) { e->arith_error = true; i = 0; }
                else i = (int32_t)prod;
            } else {
                i = x_over_n(e, x, n);
            }
        } else {
            const Glue* r = target_glue(e, tg);
            Glue q = *r;
            if (code == P_MULTIPLY) {
                int64_t w = (int64_t)q.width * n, s = (int64_t)q.stretch * n, h = (int64_t)q.shrink * n;
                if (w > 1073741823LL || w < -1073741823LL || s > 1073741823LL || s < -1073741823LL ||
                    h > 1073741823LL || h < -1073741823LL) e->arith_error = true;
                q.width = (int32_t)w; q.stretch = (int32_t)s; q.shrink = (int32_t)h;
            } else {
                q.width = x_over_n(e, q.width, n);
                q.stretch = x_over_n(e, q.stretch, n);
                q.shrink = x_over_n(e, q.shrink, n);
            }
            g = new_glue(e, q.width, q.stretch, q.stretch_order, q.shrink, q.shrink_order);
        }
    }
    if (e->arith_error) {
        diag(e, "tex-arith-overflow", "Arithmetic overflow");
        e->arith_error = false;
        return;
    }
    store_target(e, tg, i, g, TokSpan{}, global);
}

// TeX Live's file names: a `"' is never part of the name, as in its makecfilename
void append_file_name(StrBuf* out, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] != '"') strbuf_append_char(out, s[i]);
}

// TeX Live's scan_file_name: braced (`\input{...}') or space-terminated, where a
// `"' toggles quoting so that quoted spaces belong to the name
void scan_file_name(Engine* e, StrBuf* name) {
    Token t;
    skip_spaces_x(e, &t);
    if (t.kind == TK_CHAR && t.cat == CAT_BEGIN) {
        back_input(e, t);
        lam::ArrayList<Token> text(MEM_CAT_INPUT_OTHER, 16);
        scan_text_into(e, true, &text);
        StrBuf* raw = strbuf_new();
        tokens_to_str(e, span_of(text), raw, false);
        append_file_name(name, raw->str ? raw->str : "", raw->length);
        strbuf_free(raw);
        return;
    }
    bool quoted = false;
    for (;;) {
        if (t.kind != TK_CHAR || (!quoted && is_spacer(e, t))) {
            if (t.kind != TK_CHAR || !is_spacer(e, t)) back_input(e, t);
            return;
        }
        if (t.value == '"') quoted = !quoted;
        else strbuf_append_utf8(name, t.value);
        if (!get_x_token(e, &t)) return;
    }
}

static int32_t find_or_add_font(Engine* e, const char* name, int32_t size, uint32_t ident) {
    for (size_t i = 1; i < e->fonts.size(); i++) {
        if (strcmp(e->fonts[i].name, name) == 0 && e->fonts[i].size == size) return (int32_t)i;
    }
    Font f = {};
    f.name = arena_copy(e, name, strlen(name));
    f.ident = ident;
    f.size = size;
    f.param_base = (uint32_t)e->font_params.size();
    f.param_count = 7;
    // Computer Modern Roman metrics scaled to the requested size
    static const int32_t CMR10[7] = {0, 218453, 109226, 72818, 282168, 655360, 72818};
    for (int k = 0; k < 7; k++) e->font_params.append((int32_t)((int64_t)CMR10[k] * size / (10 * UNITY)));
    f.hyphen_char = int_par(e, IP_DEFAULT_HYPHEN_CHAR);
    f.skew_char = int_par(e, IP_DEFAULT_SKEW_CHAR);
    e->fonts.append(f);
    return (int32_t)e->fonts.size() - 1;
}

static void new_font(Engine* e, bool global) {
    uint32_t u = get_r_token(e);
    define_cs(e, u, make_prim(P_RELAX), global);
    scan_optional_equals(e);
    StrBuf* name = strbuf_new();
    scan_file_name(e, name);
    int32_t size = 10 * UNITY;
    if (scan_keyword(e, "at")) {
        size = scan_dimen(e, false, false, false, 0);
        if (size <= 0 || size >= 2048 * UNITY) {
            diag(e, "tex-improper-at", "Improper `at' size, replaced by 10pt");
            size = 10 * UNITY;
        }
    } else if (scan_keyword(e, "scaled")) {
        int32_t s = scan_int(e);
        if (s <= 0 || s > 32768) {
            diag(e, "tex-illegal-mag", "Illegal magnification has been changed to 1000");
            s = 1000;
        }
        size = (int32_t)((int64_t)size * s / 1000);
    }
    int32_t f = find_or_add_font(e, name->str ? name->str : "", size, u);
    strbuf_free(name);
    Meaning m = {};
    m.type = MT_FONT;
    m.value = (uint32_t)f;
    define_cs(e, u, m, global);
}

static void set_font(Engine* e, int32_t f, bool global) {
    IntSlot* slot = &e->cur_font;
    if (global) {
        slot->v = f;
        slot->level = LEVEL_ONE;
        return;
    }
    if (slot->level != e->cur_level) {
        SaveEntry entry = {};
        entry.kind = SV_FONT;
        entry.level = slot->level;
        entry.old.i = slot->v;
        e->save.append(entry);
        slot->level = e->cur_level;
    }
    slot->v = f;
}

bool scan_box_spec(Engine* e) {
    if (scan_keyword(e, "to")) {
        scan_dimen(e, false, false, false, 0);
        return true;
    }
    if (scan_keyword(e, "spread")) {
        scan_dimen(e, false, false, false, 0);
        return true;
    }
    return false;
}

// \read n to \cs (TeX §482), reading beside-document input streams
static void do_read(Engine* e, bool global, bool readline) {
    int32_t n = scan_int(e);
    if (!scan_keyword(e, "to")) diag(e, "tex-missing-to", "Missing `to' inserted");
    uint32_t cs = get_r_token(e);
    lam::ArrayList<Token> list(MEM_CAT_INPUT_OTHER, 16);
    // \read continues over lines while braces are unbalanced; past the end of the file it
    // still reads one empty line (TeX §§482-486)
    int32_t balance = 0;
    do {
        StrBuf* line = strbuf_new();
        bool eof = false;
        bool open = n >= 0 && n <= 15 && e->in_streams[n].open;
        read_stream_line(e, n, line, &eof);
        if (!open) { strbuf_free(line); break; }
        const char* text = line->str ? line->str : "";
        if (readline) {
            // e-TeX \readline: every character is other (a space is a space), \endlinechar included
            str_to_tokens(e, text, line->length, &list);
            int32_t elc = int_par(e, IP_END_LINE_CHAR);
            if (elc >= 0 && elc <= 0x10ffff) {
                char enc[8];
                size_t nl = str_utf8_encode((uint32_t)elc, enc, sizeof(enc));
                str_to_tokens(e, enc, nl, &list);
            }
        } else {
            balance = tokenize_read_line(e, text, line->length, balance, &list);
        }
        strbuf_free(line);
        if (eof && balance > 0) {
            diag(e, "tex-read-runaway", "File ended within \\read");
            balance = 0;
        }
    } while (balance > 0);
    Meaning mm = {};
    mm.type = MT_MACRO;
    mm.macro = make_macro(e, TokSpan{nullptr, 0}, freeze(e, list), 0, 0);
    Token em = {};
    em.kind = TK_END_MATCH;
    Token* params = (Token*)arena_alloc(e->arena, sizeof(Token));
    *params = em;
    ((Macro*)mm.macro)->params = TokSpan{params, 1};
    define_cs(e, cs, mm, global);
}

static void prefixed_command_inner(Engine* e, Token t, Meaning m, uint8_t a, bool* echo_def) {
    while (m.type == MT_PRIM && (m.value == P_GLOBAL || m.value == P_LONG || m.value == P_OUTER ||
                                 m.value == P_PROTECTED)) {
        a |= m.value == P_GLOBAL ? PF_GLOBAL : m.value == P_LONG ? PF_LONG
           : m.value == P_OUTER ? PF_OUTER : PF_PROTECTED;
        skip_spaces_and_relax(e, &t);
        m = token_meaning(e, t);
        if (!is_assignment(e, m)) {
            diag(e, "tex-bad-prefix", "You can't use a prefix with that command");
            back_input(e, t);
            return;
        }
    }
    bool global = (a & PF_GLOBAL) != 0;
    int32_t gd = int_par(e, IP_GLOBAL_DEFS);
    if (gd > 0) global = true;
    else if (gd < 0) global = false;
    if (m.type == MT_FONT) {
        set_font(e, (int32_t)m.value, global);
    } else if (m.type == MT_REGDEF) {
        Target tg;
        target_of(e, t, m, &tg);
        assign_value(e, tg, global);
    } else {
        switch (m.value) {
        case P_DEF: case P_GDEF: case P_EDEF: case P_XDEF: {
            *echo_def = true;
            bool g = global || m.value == P_GDEF || m.value == P_XDEF;
            if (int_par(e, IP_GLOBAL_DEFS) < 0) g = false;
            uint32_t cs = get_r_token(e);
            uint8_t flags = (uint8_t)(((a & PF_LONG) ? MF_LONG : 0) | ((a & PF_OUTER) ? MF_OUTER : 0) |
                                      ((a & PF_PROTECTED) ? MF_PROTECTED : 0));
            define_macro_cs(e, cs, flags, m.value == P_EDEF || m.value == P_XDEF, g);
            break;
        }
        case P_LET: {
            uint32_t cs = get_r_token(e);
            Token x;
            do {
                if (!get_token(e, &x)) return;
            } while (x.kind == TK_CHAR && x.cat == CAT_SPACE);
            if (x.kind == TK_CHAR && x.cat == CAT_OTHER && x.value == '=') {
                if (!get_token(e, &x)) return;
                if (x.kind == TK_CHAR && x.cat == CAT_SPACE && !get_token(e, &x)) return;
            }
            define_cs(e, cs, meaning_for_let(e, x), global);
            break;
        }
        case P_FUTURELET: {
            uint32_t cs = get_r_token(e);
            Token q, x;
            if (!get_token(e, &q) || !get_token(e, &x)) return;
            back_input(e, x);
            back_input(e, q);
            define_cs(e, cs, meaning_for_let(e, x), global);
            break;
        }
        case P_CHARDEF: case P_MATHCHARDEF: case P_COUNTDEF: case P_DIMENDEF: case P_SKIPDEF:
        case P_MUSKIPDEF: case P_TOKSDEF: {
            uint32_t cs = get_r_token(e);
            define_cs(e, cs, make_prim(P_RELAX), global);
            scan_optional_equals(e);
            Meaning d = {};
            if (m.value == P_CHARDEF) {
                d.type = MT_CHARDEF;
                d.value = (uint32_t)scan_char_num(e);
            } else if (m.value == P_MATHCHARDEF) {
                int32_t v = scan_int(e);
                if (v < 0 || v > 0x8000) {
                    diag(e, "tex-bad-mathchar", "Bad mathchar (%d)", v);
                    v = 0;
                }
                d.type = MT_MATHCHARDEF;
                d.value = (uint32_t)v;
            } else {
                d.type = MT_REGDEF;
                d.sub = m.value == P_COUNTDEF ? RK_COUNT : m.value == P_DIMENDEF ? RK_DIMEN
                      : m.value == P_SKIPDEF ? RK_SKIP : m.value == P_MUSKIPDEF ? RK_MUSKIP : RK_TOKS;
                d.value = (uint32_t)scan_register_num(e);
            }
            define_cs(e, cs, d, global);
            break;
        }
        case P_COUNT: case P_DIMEN: case P_SKIP: case P_MUSKIP: case P_TOKS:
        case P_INT_PARAM: case P_DIMEN_PARAM: case P_GLUE_PARAM: case P_MU_GLUE_PARAM:
        case P_TOKS_PARAM: {
            Target tg;
            target_of(e, t, m, &tg);
            assign_value(e, tg, global);
            break;
        }
        case P_ADVANCE: case P_MULTIPLY: case P_DIVIDE:
            do_register_command(e, m.value, global);
            break;
        case P_CATCODE: case P_LCCODE: case P_UCCODE: case P_SFCODE: case P_MATHCODE: case P_DELCODE: {
            uint8_t table = m.value == P_CATCODE ? CT_CAT : m.value == P_LCCODE ? CT_LC
                : m.value == P_UCCODE ? CT_UC : m.value == P_SFCODE ? CT_SF
                : m.value == P_MATHCODE ? CT_MATH : CT_DEL;
            uint32_t cp = (uint32_t)scan_char_num(e);
            scan_optional_equals(e);
            int32_t v = scan_int(e);
            int32_t limit = table == CT_CAT ? 15 : table == CT_MATH ? 0x8000
                : table == CT_SF ? 0x7fff : table == CT_DEL ? 0xffffff : 0x10ffff;
            if ((v < 0 && table != CT_DEL) || v > limit) {
                diag(e, "tex-invalid-code", "Invalid code (%d), should be at most %d", v, limit);
                v = 0;
            }
            set_code(e, table, cp, v, global);
            break;
        }
        case P_FONT: new_font(e, global); break;
        case P_NULLFONT: set_font(e, 0, global); break;
        case P_FONTDIMEN: {
            int32_t n = scan_int(e);
            int64_t k = font_dimen_index(e, scan_font_ident(e), n);
            scan_optional_equals(e);
            int32_t v = scan_dimen(e, false, false, false, 0);
            // an index, not a pointer: scanning the value may grow font memory
            if (k >= 0) e->font_params[(size_t)k] = v;
            break;
        }
        case P_HYPHENCHAR: case P_SKEWCHAR: {
            int32_t f = scan_font_ident(e);
            scan_optional_equals(e);
            int32_t v = scan_int(e);
            if (m.value == P_HYPHENCHAR) e->fonts[(size_t)f].hyphen_char = v;
            else e->fonts[(size_t)f].skew_char = v;
            break;
        }
        case P_TEXTFONT: case P_SCRIPTFONT: case P_SCRIPTSCRIPTFONT: {
            int32_t fam = scan_int(e) & 15;
            scan_optional_equals(e);
            int32_t f = scan_font_ident(e);
            int size = m.value == P_TEXTFONT ? 0 : m.value == P_SCRIPTFONT ? 1 : 2;
            e->fam_fonts[size][fam].v = f;
            break;
        }
        case P_SETBOX: {
            int32_t n = scan_register_num(e);
            scan_optional_equals(e);
            Token b;
            skip_spaces_and_relax(e, &b);
            Meaning mb = token_meaning(e, b);
            begin_box(e, b, mb, n, global, nullptr, nullptr);
            break;
        }
        case P_PARSHAPE: {
            scan_optional_equals(e);
            int32_t n = scan_int(e);
            for (int32_t i = 0; i < 2 * n && i < 4096; i++) scan_dimen(e, false, false, false, 0);
            break;
        }
        case P_INTERLINEPENALTIES: case P_CLUBPENALTIES: case P_WIDOWPENALTIES:
        case P_DISPLAYWIDOWPENALTIES: {
            scan_optional_equals(e);
            int32_t n = scan_int(e);
            for (int32_t i = 0; i < n && i < 4096; i++) scan_int(e);
            break;
        }
        case P_READ: case P_READLINE: do_read(e, global, m.value == P_READLINE); break;
        case P_WD: case P_HT: case P_DP: {
            int32_t n = scan_register_num(e);
            scan_optional_equals(e);
            int32_t v = scan_dimen(e, false, false, false, 0);
            Box* box = (Box*)reg_slot(e, RK_BOX, (uint32_t)n)->v.b;
            if (box) {
                if (m.value == P_WD) box->width = v;
                else if (m.value == P_HT) box->height = v;
                else box->depth = v;
            }
            break;
        }
        case P_PAGEDIMEN:
            scan_optional_equals(e);
            scan_dimen(e, false, false, false, 0);
            break;
        case P_INTERACTIONMODE:
            scan_optional_equals(e);
            scan_int(e);
            break;
        case P_READONLY_INT: {
            // \spacefactor, \prevdepth and friends are settable in TeX; values are not modeled
            scan_optional_equals(e);
            if (m.sub == RO_PREV_DEPTH) scan_dimen(e, false, false, false, 0);
            else if (m.sub == RO_SPACE_FACTOR || m.sub == RO_PREV_GRAF || m.sub == RO_DEAD_CYCLES ||
                     m.sub == RO_INSERT_PENALTIES) scan_int(e);
            else diag(e, "tex-cant-assign", "You can't assign to a read-only quantity");
            break;
        }
        case P_PATTERNS: case P_HYPHENATION:
            skip_text(e, false);
            break;
        case P_LETTERSPACEFONT: {
            uint32_t u = get_r_token(e);
            int32_t f = scan_font_ident(e);
            scan_int(e);
            Meaning fm = {};
            fm.type = MT_FONT;
            fm.value = (uint32_t)f;
            define_cs(e, u, fm, global);
            break;
        }
        default:
            break;
        }
    }
    if (e->has_after_assignment) {
        e->has_after_assignment = false;
        back_input(e, e->after_assignment);
    }
}

void prefixed_command(Engine* e, Token t, Meaning m, uint8_t a) {
    // a document-level \def stays visible to the script analysis, like \newcommand
    EchoMark em = echo_begin(e, t);
    bool echo_def = false;
    prefixed_command_inner(e, t, m, a, &echo_def);
    if (echo_def) echo_finish(e, em);
    else echo_cancel(e, em);
}

// ======================================================================
// boxes
// ======================================================================

const Box* take_box(Engine* e, int32_t n, bool copy) {
    RegSlot* slot = reg_slot(e, RK_BOX, (uint32_t)n);
    const Box* box = slot->v.b;
    if (!copy) slot->v.b = nullptr;
    return box;
}

void emit_box_items(Engine* e, const Box* box) {
    if (!box) return;
    for (uint32_t i = 0; i < box->count; i++) e->out.append(box->items[i]);
}

// begin a box for \setbox (target >= 0), an inline use (open/close text),
// or a discarded context (target == -2)
void begin_box(Engine* e, const Token& t, const Meaning& m, int32_t target, bool global,
               const char* open_text, const char* close_text) {
    if (m.type != MT_PRIM) {
        diag(e, "tex-missing-box", "A <box> was supposed to be here");
        back_input(e, t);
        return;
    }
    switch (m.value) {
    case P_BOX: case P_COPY: case P_VSPLIT: case P_LASTBOX: {
        const Box* box = nullptr;
        if (m.value == P_LASTBOX) {
            box = nullptr;
        } else {
            int32_t n = scan_register_num(e);
            if (m.value == P_VSPLIT) {
                if (!scan_keyword(e, "to")) diag(e, "tex-missing-to", "Missing `to' inserted");
                scan_dimen(e, false, false, false, 0);
            }
            box = take_box(e, n, m.value == P_COPY);
        }
        if (target >= 0) {
            RegValue v = {};
            v.b = box;
            set_reg(e, RK_BOX, (uint32_t)target, v, global);
        } else if (target == -1) {
            if (open_text) emit_text(e, open_text, strlen(open_text));
            emit_box_items(e, box);
            if (close_text) emit_text(e, close_text, strlen(close_text));
        }
        return;
    }
    case P_HBOX: case P_VBOX: case P_VTOP: {
        scan_box_spec(e);
        if (!scan_left_brace(e)) return;
        uint8_t type = m.value == P_HBOX ? GT_HBOX : m.value == P_VBOX ? GT_VBOX : GT_VTOP;
        new_save_level(e, type);
        GroupRecord& g = e->groups.back();
        g.box_target = target;
        g.box_global = global;
        g.box_kind = m.value == P_HBOX ? 0 : 1;
        if (target == -1) {
            if (open_text) emit_text(e, open_text, strlen(open_text));
            g.emit_close = close_text != nullptr;
        }
        TokSpan every = e->toks_pars[m.value == P_HBOX ? TP_EVERY_HBOX : TP_EVERY_VBOX].v;
        if (every.count) back_list(e, every);
        return;
    }
    default:
        diag(e, "tex-missing-box", "A <box> was supposed to be here");
        back_input(e, t);
        return;
    }
}

void scan_rule_spec(Engine* e, bool vrule, int32_t* w, int32_t* h, int32_t* d) {
    *w = vrule ? 26214 : -1;
    *h = vrule ? -1 : 26214;
    *d = vrule ? -1 : 0;
    for (;;) {
        if (scan_keyword(e, "width")) { *w = scan_dimen(e, false, false, false, 0); continue; }
        if (scan_keyword(e, "height")) { *h = scan_dimen(e, false, false, false, 0); continue; }
        if (scan_keyword(e, "depth")) { *d = scan_dimen(e, false, false, false, 0); continue; }
        break;
    }
}

void append_dimen_text(StrBuf* sb, int32_t v) {
    print_scaled(v, sb);
    strbuf_append_str(sb, "pt");
}

// ======================================================================
// input streams (TeX §§482-486)
// ======================================================================

bool read_stream_line(Engine* e, int32_t n, StrBuf* line, bool* eof) {
    *eof = false;
    if (n < 0 || n > 15 || !e->in_streams[n].open) {
        // TeX would read the terminal; a document run has none (non-stop mode)
        diag(e, "tex-read-terminal", "cannot \\read from the terminal in a document run");
        *eof = true;
        return false;
    }
    InStream& in = e->in_streams[n];
    const SourceFile& f = e->files[(size_t)in.file];
    if (in.pos >= f.length) {
        in.open = false;
        *eof = true;
        return false;
    }
    uint32_t i = in.pos;
    while (i < f.length && f.data[i] != '\n' && f.data[i] != '\r') i++;
    uint32_t end = i;
    while (end > in.pos && f.data[end - 1] == ' ') end--;
    strbuf_append_str_n(line, f.data + in.pos, end - in.pos);
    if (i < f.length && f.data[i] == '\r' && i + 1 < f.length && f.data[i + 1] == '\n') i++;
    in.pos = i < f.length ? i + 1 : i;
    return true;
}

} // namespace tex

#endif // LAMBDA_NO_LATEX
