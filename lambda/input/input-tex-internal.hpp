// Internal state of the TeX expansion engine (see input-tex.hpp).
//
// The engine follows the TeX82/e-TeX expansion semantics: tokens, meanings,
// a save stack with TeX's level discipline, conditionals and macro calls.
// Typesetting is not reproduced; Radiant owns layout (Lambda_Pkg_Latex3 §9.3).
#pragma once

#include "input-tex.hpp"
#include "../../lib/arena.h"
#include "../../lib/strbuf.h"
#include "../../lib/arraylist.hpp"
#include "../../lib/hashmap_typed.hpp"
#include <string.h>

namespace tex {

enum Cat : uint8_t {
    CAT_ESCAPE = 0, CAT_BEGIN = 1, CAT_END = 2, CAT_MATH = 3, CAT_ALIGN = 4,
    CAT_EOL = 5, CAT_PARAM = 6, CAT_SUPER = 7, CAT_SUB = 8, CAT_IGNORE = 9,
    CAT_SPACE = 10, CAT_LETTER = 11, CAT_OTHER = 12, CAT_ACTIVE = 13,
    CAT_COMMENT = 14, CAT_INVALID = 15
};

enum TokKind : uint8_t {
    TK_CHAR = 0,       // value = code point, cat = catcode
    TK_CS = 1,         // value = control-sequence id (active characters included)
    TK_PARAM = 2,      // #n inside a macro body
    TK_MATCH = 3,      // #n inside a parameter text
    TK_END_MATCH = 4,  // end of a parameter text
};

enum TokFlag : uint16_t {
    TF_DIRECT = 1,     // read straight from a file reader, never stored in a list
    TF_NOEXPAND = 2,   // \noexpand marker: acts as \relax for one expansion
    TF_PARLINE = 4,    // \par produced by a blank line
};

struct Token {
    uint32_t value;
    uint8_t kind;
    uint8_t cat;
    uint16_t flags;
    int32_t file;      // source file index, -1 when created without source
    uint32_t start;    // byte span in that file
    uint32_t end;
    uint32_t seq;      // token sequence number within its file reader
};

struct TokSpan {
    const Token* data;
    uint32_t count;
};

// ---------------------------------------------------------------- meanings

enum MeaningType : uint8_t {
    MT_UNDEFINED = 0,  // value = own cs id (passes through under that name)
    MT_CONSTRUCTOR,    // a document command owned by a script adapter; value = cs id
    MT_PRIM,           // value = primitive code
    MT_MACRO,
    MT_CHAR,           // \let to a character token; value = code, cat = catcode
    MT_CHARDEF,        // value = character code
    MT_MATHCHARDEF,    // value = math code
    MT_REGDEF,         // sub = register kind, value = index
    MT_FONT,           // value = font id
};

enum MacroFlag : uint8_t {
    MF_LONG = 1, MF_OUTER = 2, MF_PROTECTED = 4, MF_LATEX_OPT = 8,
};

// one argument of an xparse-style (\NewDocumentCommand) signature
struct ArgSpec {
    char type;         // m o O s t r R d D g G v b
    bool long_arg;
    Token open, close; // delimiters for t r R d D
    TokSpan dflt;      // default for O R D G
};

struct Macro {
    TokSpan params;    // parameter text: TK_MATCH, delimiters, TK_END_MATCH
    TokSpan body;      // TK_PARAM tokens reference arguments
    TokSpan opt_default;  // LaTeX optional-argument default (MF_LATEX_OPT)
    const ArgSpec* xspec; // xparse signature, collected instead of params
    uint32_t env_name;    // xparse environment owning a `b` argument
    uint8_t xcount;
    uint8_t nargs;
    uint8_t flags;
};

struct Meaning {
    uint8_t type;
    uint8_t sub;
    uint8_t cat;
    uint8_t pad;
    uint32_t value;
    const Macro* macro;
};

// ---------------------------------------------------------------- registers

enum RegKind : uint8_t { RK_COUNT = 0, RK_DIMEN, RK_SKIP, RK_MUSKIP, RK_TOKS, RK_BOX, RK_COUNT_KINDS };
static const uint32_t REGISTER_LIMIT = 32768;
// the one date pdfTeX's date queries report (S12.1.1v2), matching \year/\month/\day
static const char* const FIXED_PDF_DATE = "D:20000101120000Z";

struct Glue {
    int32_t width, stretch, shrink;
    uint8_t stretch_order, shrink_order;
};

struct OutItem;
struct Box {
    OutItem* items;
    uint32_t count;
    int32_t width, height, depth;
    uint8_t kind;      // 0 hbox, 1 vbox
};

union RegValue {
    int32_t i;
    const Glue* g;
    TokSpan t;
    const Box* b;
};

struct RegSlot {
    RegValue v;
    uint16_t level;
};

// ---------------------------------------------------------------- output

enum OutKind : uint8_t {
    OUT_TOKEN = 0,     // a token, reconstructed from its source or canonically
    OUT_SPAN,          // exact bytes [start, end) of `file`
    OUT_TEXT,          // synthesized text
};

struct OutItem {
    uint8_t kind;
    Token tok;
    int32_t file;
    uint32_t start, end;
    const char* text;
    uint32_t text_len;
    uint32_t origin;   // main-file offset this item maps to when synthesized
    uint32_t last_seq; // OUT_SPAN: seq of the last token the span covers
};

// ---------------------------------------------------------------- input

enum ReaderState : uint8_t { RS_NEW_LINE = 0, RS_MID_LINE, RS_SKIP_BLANKS };

struct SourceFile {
    const char* name;  // path or "<main>"
    const char* data;
    uint32_t length;
    bool package;      // a .sty/.cls run in the preamble, where TeX ignores spaces
    bool bundled;      // from the engine's resource directory, not beside the document
    bool local;        // read from beside the document; only these count against its budget
};

struct FileReader {
    int32_t file;
    const char* buf;
    uint32_t len;
    uint32_t line_start;   // current line [line_start, line_end) after trailing-space strip
    uint32_t line_end;
    uint32_t next_line;    // offset of the following line
    uint32_t pos;
    uint32_t line_no;
    uint32_t seq;
    uint8_t state;
    bool line_loaded;
    bool eol_done;         // \endlinechar of this line already delivered
    bool end_input;        // \endinput: stop after this line
    bool scantokens;       // pseudo-file from \scantokens
    bool done;
};

enum InputKind : uint8_t { IN_FILE = 0, IN_TOKENS };

struct InputLevel {
    uint8_t kind;
    FileReader* reader;
    const Token* toks;
    uint32_t count;
    uint32_t index;
    bool is_macro_body;
    uint8_t pool_class;   // capacity class of `owned`
    Token* owned;         // a pooled buffer this level returns to the pool when popped
};

// ---------------------------------------------------------------- save stack

enum SaveKind : uint8_t {
    SV_GROUP = 0, SV_EQ, SV_REG, SV_INT_PAR, SV_DIMEN_PAR, SV_GLUE_PAR, SV_TOKS_PAR,
    SV_CODE, SV_FONT, SV_ENV_NAME,
};

enum GroupType : uint8_t {
    GT_BOTTOM = 0, GT_SIMPLE = 1, GT_HBOX = 2, GT_ADJUSTED_HBOX = 3, GT_VBOX = 4,
    GT_VTOP = 5, GT_ALIGN = 6, GT_NO_ALIGN = 7, GT_OUTPUT = 8, GT_MATH = 9,
    GT_DISC = 10, GT_INSERT = 11, GT_VCENTER = 12, GT_MATH_CHOICE = 13,
    GT_SEMI_SIMPLE = 14, GT_MATH_SHIFT = 15, GT_MATH_LEFT = 16,
    GT_ENV = 17,       // LaTeX environment opened by \begin (engine-level)
};

struct SaveEntry {
    uint8_t kind;
    uint8_t sub;
    uint16_t level;
    uint32_t index;
    union {
        Meaning m;
        RegValue r;
        int32_t i;
        Glue gv;
    } old;
    const Glue* old_glue;
};

struct GroupRecord {
    uint8_t type;
    uint32_t save_base;      // save stack depth at entry
    uint32_t origin;         // main-file offset where it opened
    uint32_t env_name;       // GT_ENV: environment cs-name id
    uint32_t after_start;    // \aftergroup tokens begin here in after_group_tokens
    uint32_t out_start;      // box groups: captured output begins here in out
    int32_t box_target;      // \setbox register, -1 for an inline box
    bool box_global;
    uint8_t box_kind;
    bool emit_close;         // inline box: write "}" when the group ends
    bool env_passthrough;    // constructor environment: \end{name} passes through
};

// ---------------------------------------------------------------- conditionals

struct CondRecord {
    uint8_t if_limit;   // fi_code, else_code, or_code, if_code
    uint8_t cur_if;     // primitive code of the test
    uint32_t if_line;
    int32_t if_file;   // where the conditional began, for the end-of-run report
    uint32_t if_line_no;
    bool unless;
};

// ---------------------------------------------------------------- names

struct NameEntry {
    const char* chars;
    uint32_t len;
    uint32_t id;
};

typedef TypedHashMap<NameEntry,
    HashMapLenStrMemberKeyOps<NameEntry, &NameEntry::chars, &NameEntry::len>> NameMap;

struct NameRec {
    const char* chars;
    uint32_t len;
};

struct EqSlot {
    Meaning m;
    uint16_t level;
};

struct CodeEntry {
    uint32_t key;      // (table << 24) | code point for code points >= 256
    int32_t value;
    uint16_t level;
};

typedef TypedHashMap<CodeEntry,
    HashMapIntegralMemberKeyOps<CodeEntry, &CodeEntry::key>> CodeMap;

enum CodeTable : uint8_t { CT_CAT = 0, CT_LC, CT_UC, CT_SF, CT_MATH, CT_DEL, CT_TABLES };

struct Font {
    const char* name;      // external name
    uint32_t ident;        // cs id of the identifier that created it
    int32_t size;          // at size (sp)
    uint32_t param_base;   // \fontdimen n lives at Engine::font_params[param_base + n - 1]
    int32_t param_count;
    int32_t hyphen_char;
    int32_t skew_char;
};

// ---------------------------------------------------------------- LaTeX layer

enum PackageKind : uint8_t { PK_ADAPTER = 0, PK_LOCAL, PK_UNKNOWN };

struct PackageRecord {
    const char* name;
    const char* options;   // comma-separated, as given
    bool is_class;
    uint8_t kind;          // PackageKind
};

// a package or class file being run (LaTeX's \@pushfilename frame)
struct PackageFrame {
    const char* name;
    const char* options;
    bool is_class;
    int32_t saved_at_catcode;
    const char* saved_package;
    const char* saved_options;
    bool saved_is_class;
    TokSpan end_hook;      // \AtEndOfPackage / \AtEndOfClass
    const char** siblings; // names still to load from the same \usepackage
    uint32_t sibling_count;
    uint32_t sibling_next;
    const char* sibling_options;
};

struct OptionDecl {
    const char* package;   // owning package name
    const char* name;      // option name; "*" for \DeclareOption*
    TokSpan code;
};

struct InStream {
    int32_t file;
    uint32_t pos;
    bool open;
};

struct Engine;

// Primitive codes. Ranges keep classes contiguous; see input-tex-prims.cpp.
enum Prim : uint32_t {
    P_NONE = 0,
    // --- expandable
    P_EXPANDAFTER, P_NOEXPAND, P_CSNAME, P_STRING, P_NUMBER, P_ROMANNUMERAL,
    P_THE, P_UNEXPANDED, P_DETOKENIZE, P_MEANING, P_FONTNAME, P_JOBNAME,
    P_INPUT, P_ENDINPUT, P_SCANTOKENS, P_EXPANDED, P_ETEXREVISION, P_STRCMP,
    P_TOPMARK, P_FIRSTMARK, P_BOTMARK, P_SPLITFIRSTMARK, P_SPLITBOTMARK,
    P_TOPMARKS, P_FIRSTMARKS, P_BOTMARKS, P_SPLITFIRSTMARKS, P_SPLITBOTMARKS,
    P_UNIFORMDEVIATE, P_NORMALDEVIATE, P_PDFTEXREVISION, P_ESCAPESTRING, P_ESCAPENAME,
    P_ESCAPEHEX, P_UNESCAPEHEX, P_MDFIVESUM, P_FILESIZE, P_FILEMODDATE, P_CREATIONDATE,
    P_PRIMITIVE_EXP,
    // conditionals (P_IF .. P_IFINCSNAME) and their delimiters
    P_IF, P_IFCAT, P_IFNUM, P_IFDIM, P_IFODD, P_IFVMODE, P_IFHMODE, P_IFMMODE,
    P_IFINNER, P_IFVOID, P_IFHBOX, P_IFVBOX, P_IFX, P_IFEOF, P_IFTRUE, P_IFFALSE,
    P_IFCASE, P_IFDEFINED, P_IFCSNAME, P_IFFONTCHAR, P_IFPDFPRIMITIVE, P_IFINCSNAME,
    P_UNLESS, P_FI, P_ELSE, P_OR,
    P_EXPANDABLE_END,
    // --- unexpandable: general
    P_RELAX, P_ENDCSNAME, P_DEF, P_GDEF, P_EDEF, P_XDEF, P_LET, P_FUTURELET,
    P_GLOBAL, P_LONG, P_OUTER, P_PROTECTED, P_CHARDEF, P_MATHCHARDEF,
    P_COUNTDEF, P_DIMENDEF, P_SKIPDEF, P_MUSKIPDEF, P_TOKSDEF,
    P_COUNT, P_DIMEN, P_SKIP, P_MUSKIP, P_TOKS, P_ADVANCE, P_MULTIPLY, P_DIVIDE,
    P_CATCODE, P_LCCODE, P_UCCODE, P_SFCODE, P_MATHCODE, P_DELCODE,
    P_INT_PARAM, P_DIMEN_PARAM, P_GLUE_PARAM, P_MU_GLUE_PARAM, P_TOKS_PARAM,
    P_READONLY_INT, P_NUMEXPR, P_DIMEXPR, P_GLUEEXPR, P_MUEXPR,
    P_GLUESTRETCH, P_GLUESHRINK, P_GLUESTRETCHORDER, P_GLUESHRINKORDER,
    P_GLUETOMU, P_MUTOGLUE, P_FONTCHARWD, P_FONTCHARHT, P_FONTCHARDP, P_FONTCHARIC,
    P_BEGINGROUP, P_ENDGROUP, P_AFTERGROUP, P_AFTERASSIGNMENT, P_IGNORESPACES,
    P_UPPERCASE, P_LOWERCASE, P_MESSAGE, P_ERRMESSAGE, P_SHOW, P_SHOWTHE,
    P_SHOWBOX, P_SHOWLISTS, P_SHOWTOKENS, P_SHOWGROUPS, P_SHOWIFS,
    P_IMMEDIATE, P_WRITE, P_OPENOUT, P_CLOSEOUT, P_OPENIN, P_CLOSEIN, P_READ,
    P_READLINE, P_SPECIAL, P_INTERACTION, P_INTERACTIONMODE, P_END, P_DUMP,
    P_FONT, P_NULLFONT, P_FONTDIMEN, P_HYPHENCHAR, P_SKEWCHAR, P_TEXTFONT,
    P_SCRIPTFONT, P_SCRIPTSCRIPTFONT, P_PATTERNS, P_HYPHENATION, P_SETLANGUAGE,
    P_PARSHAPE, P_INTERLINEPENALTIES, P_CLUBPENALTIES, P_WIDOWPENALTIES,
    P_DISPLAYWIDOWPENALTIES, P_PRIMITIVE, P_LETTERSPACEFONT, P_SETBOX,
    P_WD, P_HT, P_DP, P_PAGEDIMEN, P_PDFSTRING_DROP, P_PDFRESETTIMER, P_SETRANDOMSEED,
    // --- typesetting
    P_PAR, P_INDENT, P_NOINDENT, P_HSKIP, P_VSKIP, P_HFIL, P_HFILL, P_HSS,
    P_HFILNEG, P_VFIL, P_VFILL, P_VSS, P_VFILNEG, P_KERN, P_MKERN, P_MSKIP,
    P_PENALTY, P_UNPENALTY, P_UNKERN, P_UNSKIP, P_HBOX, P_VBOX, P_VTOP, P_BOX,
    P_COPY, P_LASTBOX, P_VSPLIT, P_UNHBOX, P_UNHCOPY, P_UNVBOX, P_UNVCOPY,
    P_RAISE, P_LOWER, P_MOVELEFT, P_MOVERIGHT, P_HRULE, P_VRULE, P_LEADERS,
    P_CLEADERS, P_XLEADERS, P_MARK, P_MARKS, P_INSERT, P_VADJUST, P_HALIGN,
    P_VALIGN, P_NOALIGN, P_OMIT, P_SPAN, P_CR, P_CRCR, P_CHAR, P_ACCENT,
    P_DISCRETIONARY, P_DISC_HYPHEN, P_ITAL_CORR, P_CONTROL_SPACE, P_SHIPOUT,
    P_NOBOUNDARY, P_PAGEDISCARDS, P_VCENTER, P_MATH_EMIT, P_MATH_DELIM_EMIT,
    P_INSERTPENALTIES_DROP,
    // --- LaTeX layer (input-tex-latex.cpp)
    P_LATEX_FIRST,
    P_NEWCOMMAND, P_RENEWCOMMAND, P_PROVIDECOMMAND, P_DECLAREROBUSTCOMMAND,
    P_NEWENVIRONMENT, P_RENEWENVIRONMENT, P_BEGIN, P_END_ENV,
    P_NEWCOUNTER, P_SETCOUNTER, P_ADDTOCOUNTER, P_STEPCOUNTER, P_REFSTEPCOUNTER,
    P_NEWLENGTH, P_SETLENGTH, P_ADDTOLENGTH, P_DOCUMENTCLASS, P_LOADCLASS,
    P_USEPACKAGE, P_REQUIREPACKAGE, P_DECLAREOPTION, P_EXECUTEOPTIONS,
    P_PROCESSOPTIONS, P_PASSOPTIONS, P_YARGDEF, P_FILENAME_PARSE,
    P_IFPACKAGEWITH, P_IFFILEEXISTS, P_INPUTIFFILEEXISTS, P_ATBEGINDOCUMENT,
    P_ATENDDOCUMENT, P_ATENDOFPACKAGE, P_ATENDOFCLASS, P_VERB, P_RAW_GROUP_CMD,
    P_LAMBDA_CONSTRUCTOR, P_PROVIDESPACKAGE, P_INCLUDE, P_LATEX_INPUT,
    P_NEWIF, P_IFNEXTCHAR, P_IFSTAR, P_FOR, P_TFOR, P_COUNTER_FMT, P_VALUE,
    P_MATH_SWITCH, P_ROMAN_UPPER, P_PACKAGE_END, P_PACKAGE_POP, P_ENV_END_GROUP, P_DOCUMENT_END,
    P_NEWDOCUMENTCOMMAND, P_IFNOVALUE, P_IFBOOLEAN, P_ADDTOHOOK, P_IFFORMATATLEAST,
    P_SETTOBOX, P_NEEDSTEXFORMAT, P_LOADCLASSWITHOPTIONS, P_NOVALUE_CALL,
    P_COUNT_PRIMS
};

// Integer parameters (TeX82 order, then e-TeX/pdfTeX additions).
enum IntPar : uint32_t {
    IP_PRETOLERANCE, IP_TOLERANCE, IP_LINE_PENALTY, IP_HYPHEN_PENALTY,
    IP_EX_HYPHEN_PENALTY, IP_CLUB_PENALTY, IP_WIDOW_PENALTY, IP_DISPLAY_WIDOW_PENALTY,
    IP_BROKEN_PENALTY, IP_BIN_OP_PENALTY, IP_REL_PENALTY, IP_PRE_DISPLAY_PENALTY,
    IP_POST_DISPLAY_PENALTY, IP_INTER_LINE_PENALTY, IP_DOUBLE_HYPHEN_DEMERITS,
    IP_FINAL_HYPHEN_DEMERITS, IP_ADJ_DEMERITS, IP_MAG, IP_DELIMITER_FACTOR,
    IP_LOOSENESS, IP_TIME, IP_DAY, IP_MONTH, IP_YEAR, IP_SHOW_BOX_BREADTH,
    IP_SHOW_BOX_DEPTH, IP_HBADNESS, IP_VBADNESS, IP_PAUSING, IP_TRACING_ONLINE,
    IP_TRACING_MACROS, IP_TRACING_STATS, IP_TRACING_PARAGRAPHS, IP_TRACING_PAGES,
    IP_TRACING_OUTPUT, IP_TRACING_LOST_CHARS, IP_TRACING_COMMANDS,
    IP_TRACING_RESTORES, IP_UC_HYPH, IP_OUTPUT_PENALTY, IP_MAX_DEAD_CYCLES,
    IP_HANG_AFTER, IP_FLOATING_PENALTY, IP_GLOBAL_DEFS, IP_CUR_FAM,
    IP_ESCAPE_CHAR, IP_DEFAULT_HYPHEN_CHAR, IP_DEFAULT_SKEW_CHAR, IP_END_LINE_CHAR,
    IP_NEW_LINE_CHAR, IP_LANGUAGE, IP_LEFT_HYPHEN_MIN, IP_RIGHT_HYPHEN_MIN,
    IP_HOLDING_INSERTS, IP_ERROR_CONTEXT_LINES,
    IP_TRACING_ASSIGNS, IP_TRACING_GROUPS, IP_TRACING_IFS, IP_TRACING_SCAN_TOKENS,
    IP_TRACING_NESTING, IP_PRE_DISPLAY_DIRECTION, IP_LAST_LINE_FIT,
    IP_SAVING_VDISCARDS, IP_SAVING_HYPH_CODES, IP_TEXXET_STATE,
    IP_PDF_OUTPUT, IP_PDF_COMPRESS_LEVEL, IP_PDF_DECIMAL_DIGITS, IP_PDF_MINOR_VERSION,
    IP_PDF_OBJ_COMPRESS_LEVEL, IP_PDF_PK_RESOLUTION, IP_PDF_DRAFTMODE,
    IP_PDF_GENTOUNICODE, IP_PDF_ADJUST_SPACING, IP_PDF_PROTRUDE_CHARS,
    IP_PDF_TRACING_FONTS, IP_PDF_IMAGE_RESOLUTION, IP_PDF_INCLUSION_ERRORLEVEL,
    IP_COUNT
};

enum DimenPar : uint32_t {
    DP_PAR_INDENT, DP_MATH_SURROUND, DP_LINE_SKIP_LIMIT, DP_HSIZE, DP_VSIZE,
    DP_MAX_DEPTH, DP_SPLIT_MAX_DEPTH, DP_BOX_MAX_DEPTH, DP_HFUZZ, DP_VFUZZ,
    DP_DELIMITER_SHORTFALL, DP_NULL_DELIMITER_SPACE, DP_SCRIPT_SPACE,
    DP_PRE_DISPLAY_SIZE, DP_DISPLAY_WIDTH, DP_DISPLAY_INDENT, DP_OVERFULL_RULE,
    DP_HANG_INDENT, DP_H_OFFSET, DP_V_OFFSET, DP_EMERGENCY_STRETCH,
    DP_PDF_PAGE_WIDTH, DP_PDF_PAGE_HEIGHT, DP_PDF_H_ORIGIN, DP_PDF_V_ORIGIN,
    DP_PDF_LINK_MARGIN, DP_PDF_DEST_MARGIN, DP_PDF_THREAD_MARGIN, DP_PDF_PX_DIMEN,
    DP_COUNT
};

enum GluePar : uint32_t {
    GP_LINE_SKIP, GP_BASELINE_SKIP, GP_PAR_SKIP, GP_ABOVE_DISPLAY_SKIP,
    GP_BELOW_DISPLAY_SKIP, GP_ABOVE_DISPLAY_SHORT_SKIP, GP_BELOW_DISPLAY_SHORT_SKIP,
    GP_LEFT_SKIP, GP_RIGHT_SKIP, GP_TOP_SKIP, GP_SPLIT_TOP_SKIP, GP_TAB_SKIP,
    GP_SPACE_SKIP, GP_XSPACE_SKIP, GP_PAR_FILL_SKIP,
    GP_THIN_MU_SKIP, GP_MED_MU_SKIP, GP_THICK_MU_SKIP,
    GP_COUNT
};

enum ToksPar : uint32_t {
    TP_OUTPUT, TP_EVERY_PAR, TP_EVERY_MATH, TP_EVERY_DISPLAY, TP_EVERY_HBOX,
    TP_EVERY_VBOX, TP_EVERY_JOB, TP_EVERY_CR, TP_ERR_HELP, TP_EVERY_EOF,
    TP_PDF_PAGES_ATTR, TP_PDF_PAGE_ATTR, TP_PDF_PAGE_RESOURCES, TP_PDF_PK_MODE,
    TP_COUNT
};

enum ReadonlyInt : uint32_t {
    RO_LAST_PENALTY, RO_LAST_KERN, RO_BADNESS, RO_INPUT_LINE_NO, RO_ETEX_VERSION,
    RO_CURRENT_GROUP_LEVEL, RO_CURRENT_GROUP_TYPE, RO_CURRENT_IF_LEVEL,
    RO_CURRENT_IF_TYPE, RO_CURRENT_IF_BRANCH, RO_LAST_NODE_TYPE, RO_PDFTEX_VERSION,
    RO_ELAPSED_TIME, RO_RANDOM_SEED, RO_SPACE_FACTOR, RO_PREV_GRAF, RO_DEAD_CYCLES,
    RO_INSERT_PENALTIES, RO_PREV_DEPTH, RO_PAGE_GOAL, RO_LAST_SKIP,
    RO_PARSHAPE_LENGTH, RO_PARSHAPE_INDENT, RO_PARSHAPE_DIMEN,
    RO_LAST_X_POS, RO_LAST_Y_POS, RO_SHELL_ESCAPE,
};

struct PrimName {
    uint32_t code;
    uint8_t sub;
    const char* name;
};

struct IntSlot { int32_t v; uint16_t level; };
struct GlueSlot { const Glue* v; uint16_t level; };
struct ToksSlot { TokSpan v; uint16_t level; };

struct Engine {
    EngineOptions opts;
    Arena* arena;

    // names and meanings
    NameMap names;
    lam::ArrayList<NameRec> name_list;
    lam::ArrayList<EqSlot> eqtb;

    // registers and parameters
    RegSlot* regs[RK_COUNT_KINDS];
    IntSlot int_pars[IP_COUNT];
    IntSlot dimen_pars[DP_COUNT];
    GlueSlot glue_pars[GP_COUNT];
    ToksSlot toks_pars[TP_COUNT];
    IntSlot codes[CT_TABLES][256];
    CodeMap wide_codes;
    lam::ArrayList<Font> fonts;
    lam::ArrayList<int32_t> font_params;   // TeX's font_info: parameters of every font, in load order
    IntSlot cur_font;
    IntSlot fam_fonts[3][16];

    // save stack, groups, conditionals
    lam::ArrayList<SaveEntry> save;
    lam::ArrayList<GroupRecord> groups;
    lam::ArrayList<Token> after_group_tokens;
    uint16_t cur_level;
    lam::ArrayList<CondRecord> conds;
    uint8_t if_limit;
    uint8_t cur_if;
    uint32_t if_line;
    int32_t if_file;
    uint32_t if_line_no;

    // input
    lam::ArrayList<SourceFile> files;
    lam::ArrayList<InputLevel> input;
    Token* token_pool[32];   // free transient token buffers, by power-of-two capacity
    int32_t main_file;
    int32_t read_source;   // the one source \read records are tokenized from, or -1
    uint32_t last_main_offset;

    Token after_assignment;
    bool has_after_assignment;

    // scanner state shared by scan_int/scan_dimen (TeX's cur_tok, radix, cur_order)
    Token cur_tok;
    int radix;
    uint8_t cur_order;
    bool arith_error;
    bool box_metrics_reported;
    lam::ArrayList<struct PrimName> prim_names;

    // echo tracking: direct main-file tokens read since echo_begin
    uint64_t foreign_reads;
    uint32_t echo_min_seq, echo_max_seq;
    uint32_t recent_seq[64];
    uint32_t recent_end[64];
    FileReader* main_reader;
    bool stop_requested;       // \end, \dump or \end{document}
    InStream in_streams[16];
    bool out_open[16];

    // output: one stack; box groups own the slice above their out_start
    lam::ArrayList<OutItem> out;
    uint32_t math_depth;
    bool display_math;

    // results
    lam::ArrayList<Diagnostic> diagnostics;
    lam::ArrayList<const char*> messages;
    lam::ArrayList<const char*> loaded;
    lam::ArrayList<OffsetSegment> segments;
    StrBuf* term_line;
    StrBuf* text;          // reconstructed output
    StrBuf* scratch;       // nested users take a mark and restore the length

    // LaTeX layer
    lam::ArrayList<PackageRecord> packages;
    lam::ArrayList<OptionDecl> option_decls;
    lam::ArrayList<Token> begin_document_hook;
    lam::ArrayList<Token> end_document_hook;
    lam::ArrayList<PackageFrame> package_frames;
    lam::ArrayList<const char*> pass_options;   // pairs: package, options
    lam::ArrayList<uint32_t> engine_counters;   // \c@name cs ids owned by the engine
    lam::ArrayList<uint32_t> counter_resets;    // pairs: parent cs, child cs
    const char* current_package;
    bool current_is_class;
    const char* class_options;
    const char* current_options;
    bool in_document;
    bool document_ended;
    bool expl3_format;     // expl3 is in this engine's format (loaded from it, or being built into it)
    bool wants_expl3;      // stopped to restart from the expl3 format

    // budgets and state
    uint64_t expansions;
    uint64_t max_expansions;
    bool aborted;
    uint32_t random_seed;
    char* base_dir;

    // well-known cs ids
    uint32_t cs_par, cs_relax, cs_frozen_relax, cs_endcsname, cs_begin, cs_end, cs_document;

    Result result;
};

// ---------------------------------------------------------------- shared API

struct EchoMark {
    bool valid;
    uint32_t start;
    uint32_t first_seq;
    uint64_t foreign_reads;
    uint32_t saved_min, saved_max;
};

EchoMark echo_begin(Engine* e, const Token& t);
bool echo_finish(Engine* e, const EchoMark& m);

// names
uint32_t intern(Engine* e, const char* chars, uint32_t len);
uint32_t intern_cstr(Engine* e, const char* chars);
uint32_t intern_active(Engine* e, uint32_t code);
const NameRec& name_of(Engine* e, uint32_t id);
bool name_is_active(const NameRec& rec, uint32_t* code);

// meanings
const Meaning& meaning_of(Engine* e, uint32_t cs);
Meaning token_meaning(Engine* e, const Token& t);
void define_cs(Engine* e, uint32_t cs, const Meaning& m, bool global);
Meaning make_prim(uint32_t code, uint8_t sub = 0);
bool meanings_equal(Engine* e, const Meaning& a, const Meaning& b);
bool is_expandable(const Meaning& m);
void define_primitive(Engine* e, const char* name, uint32_t code, uint8_t sub = 0);
const char* prim_name(Engine* e, uint32_t code, uint8_t sub);

// codes and parameters
int32_t get_code(Engine* e, uint8_t table, uint32_t cp);
void set_code(Engine* e, uint8_t table, uint32_t cp, int32_t value, bool global);
int32_t int_par(Engine* e, uint32_t which);
void set_int_par(Engine* e, uint32_t which, int32_t value, bool global);
void set_dimen_par(Engine* e, uint32_t which, int32_t value, bool global);
void set_glue_par(Engine* e, uint32_t which, const Glue* value, bool global);
void set_toks_par(Engine* e, uint32_t which, TokSpan value, bool global);
RegSlot* reg_slot(Engine* e, uint8_t kind, uint32_t index);
void set_reg(Engine* e, uint8_t kind, uint32_t index, RegValue v, bool global);

// groups
void new_save_level(Engine* e, uint8_t type);
void unsave(Engine* e);

// input
void push_file(Engine* e, int32_t file, bool scantokens);
void push_tokens(Engine* e, const Token* toks, uint32_t count, bool macro_body);
// TeX's main memory bound for one token list: false (reported, run stopped) past it
bool list_fits(Engine* e, size_t count);
int32_t tokenize_read_line(Engine* e, const char* text, size_t len, int32_t balance, lam::ArrayList<Token>* out);
void back_input(Engine* e, const Token& t);
void back_list(Engine* e, TokSpan list);
void back_list_copy(Engine* e, const Token* data, uint32_t count);
void back_list_copy(Engine* e, const lam::ArrayList<Token>& list);
bool get_next(Engine* e, Token* t);          // raw; false at end of all input
bool get_token(Engine* e, Token* t);         // get_next with \outer ignored
bool get_x_token(Engine* e, Token* t);       // expands until unexpandable
bool get_x_or_protected(Engine* e, Token* t);
void expand(Engine* e, const Token& t, const Meaning& m);
int32_t add_source(Engine* e, const char* name, const char* data, uint32_t length);
FileReader* current_reader(Engine* e);

// tokens
Token make_char(uint32_t code, uint8_t cat);
Token make_cs(uint32_t id);
TokSpan freeze(Engine* e, const lam::ArrayList<Token>& list);
TokSpan freeze_range(Engine* e, const Token* data, uint32_t count);
void str_to_tokens(Engine* e, const char* s, size_t n, lam::ArrayList<Token>* out);
void tokens_to_str(Engine* e, TokSpan list, StrBuf* out, bool show_params);
void print_cs(Engine* e, uint32_t cs, StrBuf* out, bool trailing_space);
void print_meaning(Engine* e, const Meaning& m, StrBuf* out, const Token* tok);

// scanning
bool scan_keyword(Engine* e, const char* keyword);
void scan_optional_equals(Engine* e);
void skip_spaces_x(Engine* e, Token* t);
void skip_spaces_and_relax(Engine* e, Token* t);
int32_t scan_int(Engine* e);
int32_t scan_dimen(Engine* e, bool mu, bool inf, bool shortcut, int32_t shortcut_value);
int internal_level(Engine* e, const Meaning& m);
enum ValueLevel { LV_INT = 0, LV_DIMEN = 1, LV_GLUE = 2, LV_MU = 3, LV_IDENT = 4, LV_TOK = 5 };
struct Quantity {
    int level;
    int32_t i;           // LV_INT, LV_DIMEN, LV_IDENT (font id)
    const Glue* g;       // LV_GLUE, LV_MU
    TokSpan t;           // LV_TOK
};
Quantity scan_something_internal(Engine* e, const Token& t, const Meaning& m, int level, bool negative);
const Glue* new_glue(Engine* e, int32_t width, int32_t stretch, uint8_t so, int32_t shrink, uint8_t sho);
extern const Glue ZERO_GLUE;
const Glue* scan_glue(Engine* e, bool mu);
int32_t scan_register_num(Engine* e);
int32_t scan_char_num(Engine* e);
int32_t scan_font_ident(Engine* e);
int64_t font_dimen_index(Engine* e, int32_t f, int32_t n);
int32_t font_param(Engine* e, int32_t f, int32_t n);
bool scan_left_brace(Engine* e);
TokSpan scan_toks(Engine* e, bool macro_def, bool xpand, lam::ArrayList<Token>* params, uint8_t* nargs);
TokSpan scan_general_text(Engine* e);
// the same, appending to a list the caller owns: for text used and dropped, which a frozen copy would leak
void scan_toks_into(Engine* e, bool macro_def, bool xpand, lam::ArrayList<Token>* params, uint8_t* nargs,
                    lam::ArrayList<Token>* out);
void scan_text_into(Engine* e, bool xpand, lam::ArrayList<Token>* out);
void skip_text(Engine* e, bool xpand);   // a <general text> whose tokens are not kept
inline TokSpan span_of(const lam::ArrayList<Token>& list) { return TokSpan{list.data(), (uint32_t)list.size()}; }
void scan_balanced_group(Engine* e, lam::ArrayList<Token>* out);
void expand_to_list(Engine* e, TokSpan in, lam::ArrayList<Token>* out);
bool the_toks(Engine* e, lam::ArrayList<Token>* out);

// arithmetic and printing
int32_t xn_over_d(Engine* e, int32_t x, int32_t n, int32_t d, int32_t* remainder);
bool is_spacer(Engine* e, const Token& t);
void print_scaled(int32_t s, StrBuf* out);
void print_glue(const Glue* g, const char* unit, StrBuf* out);
void print_int(int32_t v, StrBuf* out);
void print_roman(int32_t v, StrBuf* out);

// conditionals
void conditional(Engine* e, const Token& t, uint32_t code, bool unless);
void fi_or_else(Engine* e, const Token& t, uint32_t code);

// macros
bool macro_call(Engine* e, const Token& t, const Meaning& m);
bool scan_macro_arg(Engine* e, lam::ArrayList<Token>* out, bool long_ok);
bool scan_bracket_arg(Engine* e, lam::ArrayList<Token>* out, bool* present);
bool scan_until_delimiter(Engine* e, const Token* delim, uint32_t dlen, lam::ArrayList<Token>* out);
bool latex_collect_xargs(Engine* e, const Token& call, const Macro* mac, lam::ArrayList<Token>* args);
void echo_cancel(Engine* e, const EchoMark& m);
const Macro* make_macro(Engine* e, TokSpan params, TokSpan body, uint8_t nargs, uint8_t flags);

// output and diagnostics
void emit_token(Engine* e, const Token& t);
void emit_text(Engine* e, const char* text, size_t len);
void emit_span(Engine* e, int32_t file, uint32_t start, uint32_t end, uint32_t first_seq, uint32_t last_seq);
void diag(Engine* e, const char* code, const char* fmt, ...);
void term_print(Engine* e, const char* text, size_t len);
void term_flush(Engine* e);
const char* arena_copy(Engine* e, const char* s, size_t n);

// main control and primitives
void init_primitives(Engine* e);
void main_control(Engine* e);
void do_unexpandable(Engine* e, const Token& t, const Meaning& m, bool main_loop);
void prefixed_command(Engine* e, Token t, Meaning m, uint8_t prefixes);
bool latex_do(Engine* e, const Token& t, uint32_t code);
void latex_init(Engine* e);
bool latex_expandable(uint32_t code);
void reconstruct(Engine* e);
bool read_stream_line(Engine* e, int32_t n, StrBuf* line, bool* eof);
void do_unexpandable_prim(Engine* e, const Token& t, const Meaning& m);
void begin_box(Engine* e, const Token& t, const Meaning& m, int32_t target, bool global,
               const char* open_text, const char* close_text);
const Box* take_box(Engine* e, int32_t n, bool copy);
void emit_box_items(Engine* e, const Box* box);
void scan_file_name(Engine* e, StrBuf* name);
void append_file_name(StrBuf* out, const char* s, size_t n);
bool scan_box_spec(Engine* e);
void scan_rule_spec(Engine* e, bool vrule, int32_t* w, int32_t* h, int32_t* d);
void append_dimen_text(StrBuf* sb, int32_t v);
void emit_passthrough(Engine* e, const Token& t, uint32_t name_id);
void handle_char(Engine* e, const Token& t, uint32_t code, uint8_t cat);
void handle_right_brace(Engine* e, const Token& t);
uint32_t get_r_token(Engine* e);
void emit_canonical_cs(Engine* e, const char* name);
void pass_constructor_args(Engine* e, bool raw);
bool latex_arg_mode_env(Engine* e, const Token& t, bool begin);
void load_kernel(Engine* e);
void run_preload(Engine* e, const char* name, const char* source, size_t length);
// 1: loaded; 0: no image, the engine is untouched; -1: the image failed midway
int load_expl3_format(Engine* e);
bool open_local_file(Engine* e, const char* name, const char* const* exts, int ext_count,
                     int32_t* file_out, bool report_missing);
bool open_input_file(Engine* e, const char* name, const char* const* exts, int ext_count,
                     int32_t* file_out, bool report_missing);

static inline bool tok_is_char(const Token& t, uint32_t code, uint8_t cat) {
    return t.kind == TK_CHAR && t.value == code && t.cat == cat;
}

} // namespace tex
