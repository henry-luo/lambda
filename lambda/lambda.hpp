#pragma once

#include "lambda/lambda.h"

// Runtime state and helper declarations use the shared input value ABI.
// Dry-run mode: when enabled, IO functions return fabricated results
// instead of performing actual network/filesystem operations
#if !defined(__cplusplus)
bool lambda_dry_run_enabled(void);
#endif

// Stack overflow protection (callable from JIT-compiled code)
#ifdef __cplusplus
extern "C" {
#endif
void lambda_stack_overflow_error(const char* func_name);
uint64_t lambda_stack_is_exhausted(uintptr_t stack_limit);
void lambda_root_frame_overflow_error(void);
#ifdef __cplusplus
}
#endif


// TCO (Tail Call Optimization) iteration limit
// Guards against infinite loops from tail-recursive functions that never terminate.
// TCO converts tail calls into goto loops, bypassing signal-based stack overflow
// detection, so we use an explicit counter.
#define LAMBDA_TCO_MAX_ITERATIONS 1000000


// System function identifiers used by the runtime registry.
typedef enum SysFunc {
    SYSFUNC_LEN,
    SYSFUNC_TYPE,
    SYSFUNC_NAME,       // name(item) - get local name of element, function, or type
    SYSFUNC_INT,
    SYSFUNC_INT64,
    SYSFUNC_FLOAT,
    SYSFUNC_COMPLEX,
    SYSFUNC_COMPLEX2,
    SYSFUNC_REAL,
    SYSFUNC_IMAG,
    SYSFUNC_CONJ,
    SYSFUNC_DECIMAL,
    SYSFUNC_NUMBER,
    SYSFUNC_STRING,
    //SYSFUNC_CHAR,
    SYSFUNC_SYMBOL,
    SYSFUNC_SYMBOL2,    // symbol(name, url) - 2 args, create namespaced symbol
    SYSFUNC_BINARY,
    SYSFUNC_DATETIME,
    SYSFUNC_DATETIME0,  // datetime() - 0 args, current datetime
    SYSFUNC_DATE,
    SYSFUNC_DATE0,      // date() - 0 args, current date
    SYSFUNC_DATE3,      // date(y,m,d) - 3 args, construct from components
    SYSFUNC_TIME,
    SYSFUNC_TIME0,      // time() - 0 args, current time
    SYSFUNC_TIME3,      // time(h,m,s) - 3 args, construct from components
    SYSFUNC_JUSTNOW,
    SYSFUNC_SET,
    SYSFUNC_SLICE,
    SYSFUNC_VIEW,             // subview(arr, start, end) - read-only view sharing arr's storage
    SYSFUNC_IS_VIEW,          // is_view(arr) - check if arr is a view
    SYSFUNC_RESHAPE,          // reshape(arr, shape_list) - view with new shape
    SYSFUNC_SHAPE,            // shape(arr) - list of dimensions
    SYSFUNC_NDIM,             // ndim(arr) - number of dimensions
    SYSFUNC_SUM2,             // sum(arr, axis) - reduce along axis
    SYSFUNC_AVG2,             // avg(arr, axis) - mean along axis
    SYSFUNC_PROD2,            // math.prod(arr, axis)
    SYSFUNC_MEAN2,            // math.mean(arr, axis)
    SYSFUNC_CUMSUM2,          // math.cumsum(arr, axis)
    SYSFUNC_CUMPROD2,         // math.cumprod(arr, axis)
    SYSFUNC_TRANSPOSE,        // transpose(arr) - view with reversed axes (zero-copy)
    SYSFUNC_FLATTEN,          // flatten(arr) - owned 1-D contiguous copy
    SYSFUNC_RAVEL,            // ravel(arr) - 1-D view if contiguous, else copy
    SYSFUNC_MATMUL,           // matmul(a, b) - matrix product (2-D·2-D, 1-D dot)
    SYSFUNC_CONCAT,           // concat(a, b) - join along axis 0
    SYSFUNC_STACK,            // stack(a, b) - stack along a new leading axis
    // image stencil engine (N-D windowed neighbourhood ops over ArrayNum)
    SYSFUNC_CONVOLVE,         // convolve(img, kernel) - weighted-sum (correlation)
    SYSFUNC_BLUR,             // blur(img, ksize) - box (mean) blur
    SYSFUNC_ERODE,            // erode(img, ksize) - morphological min
    SYSFUNC_DILATE,           // dilate(img, ksize) - morphological max
    SYSFUNC_MEDIAN_FILT,      // median_filter(img, ksize) - rank (median) filter
    SYSFUNC_MAXPOOL,          // maxpool(img, ksize) - strided max pooling
    SYSFUNC_AVGPOOL,          // avgpool(img, ksize) - strided mean pooling
    // image I/O bridge
    SYSFUNC_LOAD_IMAGE,       // load(path) - decode PNG/JPEG/GIF to (H,W,4) ubyte
    SYSFUNC_SAVE_IMAGE,       // save(img, path) - encode an image to PNG
    SYSFUNC_AS_FLOAT,         // as_float(img) - ubyte [0,255] -> float [0,1]
    SYSFUNC_AS_UBYTE,         // as_ubyte(img) - float [0,1] -> ubyte [0,255]
    // point / colour / geometric image ops
    SYSFUNC_INVERT,           // invert(img) - photographic negative
    SYSFUNC_GAMMA,            // gamma(img, g) - gamma correction
    SYSFUNC_THRESHOLD,        // threshold(img, t) - binarize at t
    SYSFUNC_GRAYSCALE,        // grayscale(img) - RGB -> luma (H,W)
    SYSFUNC_FLIP,             // flip(img, axis) - mirror along axis 0/1
    SYSFUNC_ROT90,            // rot90(img, k) - rotate CCW by 90*k
    SYSFUNC_CROP,             // crop(img, rows, cols) - region copy
    // histogram / segmentation / resize / warp
    SYSFUNC_HISTOGRAM,        // histogram(img, bins) - value counts
    SYSFUNC_OTSU,             // otsu(img) - optimal threshold value
    SYSFUNC_LABEL,            // label(mask) - 4-connected components
    SYSFUNC_RESIZE,           // resize(img, h, w) - bilinear resample
    SYSFUNC_ROTATE,           // rotate(img, deg) - bilinear rotation
    SYSFUNC_AFFINE_WARP,      // affine_warp(img, M) - 2x3 affine gather
    SYSFUNC_IMAGE_PROCESS,    // image_process(img, options) - bounded RGBA pipeline
    SYSFUNC_ALL,
    SYSFUNC_ANY,
    SYSFUNC_MIN1,
    SYSFUNC_MIN2,
    SYSFUNC_MAX1,
    SYSFUNC_MAX2,
    SYSFUNC_SUM,
    SYSFUNC_AVG,
    SYSFUNC_ABS,
    SYSFUNC_ROUND,
    SYSFUNC_FLOOR,
    SYSFUNC_CEIL,
    SYSFUNC_INPUT1,
    SYSFUNC_INPUT2,
    SYSFUNC_FORMAT1,
    SYSFUNC_FORMAT2,
    SYSFUNC_ERROR,
    SYSFUNC_ERROR2,         // error(message, source) - wraps an inner error (S7.4.4)
    SYSFUNC_EXISTS,         // exists(path) - check if file/dir exists
    // PTH76: `temp(name, content)` creates the in-memory document temp.'name'
    // with `content` as its head and returns that head; `temp(name)` returns an
    // existing head, creating an empty document when there is none. Creation is
    // IMMEDIATE and outside any write set -- document management like io.mkdir,
    // not a Tier-3 edit -- which is what lets the document be read in the same
    // evaluation without a commit.
    SYSFUNC_TEMP1,
    SYSFUNC_TEMP2,
    SYSFUNC_NORMALIZE,
    SYSFUNC_NORMALIZE2,     // normalize(str, form) with 2 args
    // string functions
    SYSFUNC_CONTAINS,
    SYSFUNC_STARTS_WITH,
    SYSFUNC_ENDS_WITH,
    SYSFUNC_INDEX_OF,
    SYSFUNC_LAST_INDEX_OF,
    SYSFUNC_TRIM,
    SYSFUNC_TRIM_START,
    SYSFUNC_TRIM_END,
    SYSFUNC_LOWER,
    SYSFUNC_UPPER,
    SYSFUNC_URL_RESOLVE,
    SYSFUNC_SPLIT,
    SYSFUNC_SPLIT3,         // split(str, sep, keep_delim) with 3 args
    SYSFUNC_JOIN,           // join(strs, sep) for strings
    SYSFUNC_REPLACE,
    SYSFUNC_REPLACE4,       // replace(str, old, new, options) - with options
    SYSFUNC_FIND,           // find(str, pattern) - find all matches
    SYSFUNC_FIND3,          // find(str, pattern, options) - with options
    SYSFUNC_ORD,            // ord(str) - Unicode code point of first character
    SYSFUNC_CHR,            // chr(int) - character from Unicode code point
    SYSFUNC_IO_CELL_WIDTH,  // io.cell_width(int) - terminal cell width of a scalar
    SYSFUNC_IO_UNICODE_CATEGORY, // io.unicode_category(int) - Unicode general category
    // vector functions
    SYSFUNC_PROD,
    SYSFUNC_CUMSUM,
    SYSFUNC_CUMPROD,
    SYSFUNC_ARGMIN,
    SYSFUNC_ARGMAX,
    SYSFUNC_FILL,
    SYSFUNC_DOT,
    SYSFUNC_NORM,
    // statistical functions
    SYSFUNC_MEAN,
    SYSFUNC_MEDIAN,
    SYSFUNC_MEDIAN2,
    SYSFUNC_VARIANCE,
    SYSFUNC_VARIANCE2,
    SYSFUNC_DEVIATION,
    SYSFUNC_DEVIATION2,
    // element-wise math functions
    SYSFUNC_SQRT,
    SYSFUNC_LOG,
    SYSFUNC_LOG10,
    SYSFUNC_EXP,
    SYSFUNC_SIN,
    SYSFUNC_COS,
    SYSFUNC_TAN,
    // inverse trigonometric
    SYSFUNC_ASIN,
    SYSFUNC_ACOS,
    SYSFUNC_ATAN,
    SYSFUNC_ATAN2,
    // hyperbolic
    SYSFUNC_SINH,
    SYSFUNC_COSH,
    SYSFUNC_TANH,
    // inverse hyperbolic
    SYSFUNC_ASINH,
    SYSFUNC_ACOSH,
    SYSFUNC_ATANH,
    // exponential/logarithmic variants
    SYSFUNC_EXP2,
    SYSFUNC_EXPM1,
    SYSFUNC_LOG2,
    // power/root
    SYSFUNC_POW_MATH,
    SYSFUNC_CBRT,
    SYSFUNC_TRUNC,
    SYSFUNC_HYPOT,
    SYSFUNC_LOG1P,
    SYSFUNC_SIGN,
    SYSFUNC_CLIP,           // clip(x, lo, hi) - element-wise clamp to [lo, hi]
    // random number generation
    SYSFUNC_RANDOM,
    // vector manipulation functions
    SYSFUNC_REVERSE,
    SYSFUNC_SORT,
    SYSFUNC_SORT2,
    SYSFUNC_UNIQUE,
    SYSFUNC_TAKE,
    SYSFUNC_DROP,
    SYSFUNC_ZIP,
    SYSFUNC_RANGE3,
    SYSFUNC_QUANTILE,
    SYSFUNC_QUANTILE3,
    SYSFUNC_REDUCE,         // reduce(collection, fn) - fold/accumulate
    // parse string functions
    SYSFUNC_PARSE1,         // parse(str) - parse string, auto-detect format
    SYSFUNC_PARSE2,         // parse(str, format) - parse string with format
    SYSFUNC_PARSE_HTML_FRAGMENT,
    // variadic parameter access
    SYSFUNC_VARG0,          // varg() - get all variadic args as list
    SYSFUNC_VARG1,          // varg(n) - get nth variadic arg
    // bitwise functions
    SYSFUNC_BAND,
    SYSFUNC_BOR,
    SYSFUNC_BXOR,
    SYSFUNC_BNOT,
    SYSFUNC_SHL,
    SYSFUNC_SHR,
    SYSFUNC_USHR,
    SYSFUNC_TO_PROMISE,       // toPromise(handle) - JS Promise adapter
    // procedural functions
    SYSPROC_NOW,
    SYSPROC_TODAY,
    SYSPROC_PRINT,
    SYSPROC_FETCH,
    SYSPROC_OUTPUT2,         // output(source, target) - writes data to target, returns bytes
    SYSPROC_OUTPUT3,         // output(source, target, options) - with options map
    SYSPROC_CMD,
    SYSPROC_CMD1,            // cmd(command) - no args version
    // io module functions (unified I/O - supports local and remote targets)
    SYSPROC_IO_COPY,
    SYSPROC_IO_READ,          // io.read(target) - async text read
    SYSPROC_IO_MOVE,
    SYSPROC_IO_DELETE,
    SYSPROC_IO_MKDIR,
    SYSPROC_IO_TOUCH,
    SYSPROC_IO_SYMLINK,
    SYSPROC_IO_CHMOD,
    SYSPROC_IO_RENAME,
    SYSPROC_IO_FETCH,        // io.fetch(target, options) - fetch data from URL or file
    // io.http module (web server)
    SYSPROC_IO_HTTP_CREATE_SERVER,  // io.http.create_server(config?) - create HTTP server
    SYSPROC_IO_HTTP_LISTEN,         // io.http.listen(server, port) - start listening
    SYSPROC_IO_HTTP_ROUTE,          // io.http.route(server, method, path, handler)
    SYSPROC_IO_HTTP_USE,            // io.http.use(server, middleware) - add middleware
    SYSPROC_IO_HTTP_STATIC,         // io.http.static(server, url_path, dir_path)
    SYSPROC_IO_HTTP_STOP,           // io.http.stop(server) - graceful shutdown
    // vmap functions
    SYSFUNC_VMAP_NEW,        // map() or map([k1,v1,...]) - create VMap
    SYSPROC_VMAP_SET,        // m.set(k, v) - in-place insert on VMap (procedural)
    SYSFUNC_JUBE_MODULE,     // descriptor-backed native module function
    SYSPROC_CLOCK,           // clock() - high-resolution monotonic time in seconds (float)
    // file-based find/replace (procedural)
    SYSPROC_REPLACE_FILE,    // pn replace(path, pattern, repl) - sed-like file replace
    SYSPROC_REPLACE_FILE4,   // pn replace(path, pattern, repl, options)
    // S12.3.4 dynamic application. Two enum rows for one surface name `call`:
    // the lowering picks the fn- or pn-coloured entry from the ENCLOSING
    // context, which fixes the error convention (return vs raise), while the
    // TARGET's colour is what S12.1.4 checks.
    SYSFUNC_CALL,            // fn  call(f, args) - apply f to array args; returns error
    SYSPROC_CALL,            // pn  call(f, args) - apply f to array args; raises
    // view/edit template apply
    SYSFUNC_APPLY1,          // apply(target) - apply view templates to target
    SYSFUNC_APPLY2,          // apply(target, options) - apply with options map
    // edit bridge — MarkEditor operations (Phase 4)
    SYSFUNC_EDIT_UNDO,       // undo() - undo last edit commit
    SYSFUNC_EDIT_REDO,       // redo() - redo last undone commit
    SYSFUNC_EDIT_COMMIT,     // edit_commit() - commit current edits as version
    SYSFUNC_EDIT_COMMIT1,    // edit_commit(description) - commit with description
    // reactive UI event dispatch
    SYSPROC_EMIT,            // emit(event_name, data) - dispatch event to parent template handler
    SYSFUNC_PDF_PARSE_CONTENT_STREAM,  // pdf_parse_content_stream(bytes) - fast PDF content tokenizer
    SYSFUNC_PS_SCAN,         // ps_scan(bytes, offset, limits) - one bounded PostScript source object
    SYSFUNC_PS_READ,         // ps_read(bytes, offset, request) - bounded byte/codec read
    SYSFUNC_PS_PNG,          // ps_png(planes, geometry, limits) - portable image transport
    SYSFUNC_PDF_REGISTER_SVG_IMAGE_RESOLVER,  // pdf_register_svg_image_resolver(svg, pdf) - bind PDF image handles to SVG root
    SYSFUNC_CONTENT,          // content(e) - read-only array view over an element's content (LR09-9)
    SYSPROC_PUSH,            // push(arr, val) - append val to a growable array in place (procedural)
    SYSPROC_SPLICE,          // splice(arr, start, count) - remove count elements at start, in place (procedural)
    SYSPROC_START,           // start(pn, args?, options?) - launch a scoped child task
    SYSPROC_SEND,
    SYSPROC_RECEIVE,
    SYSPROC_WAIT,
    SYSPROC_SELECT,
    SYSPROC_SLEEP,
    SYSPROC_SELF,
    SYSPROC_CANCEL,
    // appended, never inserted: existing SysFunc values stay stable for
    // anything that stores them
    SYSFUNC_COUNT,      // count(x) - the size of the run x is (S8.3.3v3)
    // S10.1.1v2: the value set functions that replaced container `| & !`
    SYSFUNC_UNIQUE2,
    SYSFUNC_UNIQUE3,
    SYSFUNC_UNIQUE4,
    SYSFUNC_INTERSECT2,
    SYSFUNC_INTERSECT3,
    SYSFUNC_INTERSECT4,
    SYSFUNC_EXCEPT,
    SYSPROC_IO_GREP,         // io.grep(source, pattern, options?) - line-oriented file search (lib/grep, GRP26)
    SYSPROC_IO_TEXT_SEARCH,       // io.text_search(source, query, options?) - ranked full-text file search (lib/fts, FTX11)
    SYSPROC_IO_EXEC,              // io.exec(program, args, options?) - cancellable byte-exact process capture
    SYSPROC_MCP_REQUEST,          // mcp.request(endpoint, method, params) - request-owned MCP exchange
    SYSPROC_MCP_CALL_TOOL,        // mcp.call_tool(endpoint, name, arguments)
    SYSFUNC_PARSE3,         // parse(str, grammar, 'rule') - run a grammar from a named rule (GM5.1)
    SYSFUNC_ERF,
    SYSFUNC_ERFC,
    SYSFUNC_FFT,
    SYSFUNC_IFFT,
    SYSFUNC_RFFT,
    SYSFUNC_IRFFT,
    SYSFUNC_SOLVE,
    SYSFUNC_LSTSQ,
    SYSFUNC_ORIENT2D,
    SYSFUNC_ORIENT3D,
    SYSFUNC_INCIRCLE,
    SYSFUNC_PHYSICS_SIMULATE,
    SYSFUNC_PHYSICS_VALIDATE,
    SYSFUNC_PHYSICS_QUERY,
    SYSFUNC_NAVIGATION_PATH,
    SYSFUNC_NAVIGATION_VALIDATE,
    SYSFUNC_BINARY_DECODE,
    SYSFUNC_IMAGE_RASTER,
    SYSFUNC_IMAGE_REGION,
    SYSFUNC_IMAGE_RENDER,
    SYSFUNC_IMAGE_DECODE,
    SYSFUNC_IMAGE_ENCODE,
    SYSFUNC_IMAGE_WRITE_REGION,
    SYSFUNC_IMAGE_PATCH,
    SYSFUNC_IMAGE_CAPABILITIES,
    SYSFUNC_IMAGE_EDIT,
    SYSPROC_IMAGE_STORAGE,
    SYSPROC_IO_MAIL_OPEN,         // lambda.io.mail open(url, options) - log in to an IMAP or POP3 server
    SYSPROC_IO_MAIL_COMMAND,      // lambda.io.mail command(session, command, args?, options?)
    SYSPROC_IO_MAIL_IDLE,         // lambda.io.mail idle(session, seconds) - IMAP IDLE (RFC 2177)
    SYSPROC_IO_MAIL_SEND,         // lambda.io.mail send(url, options, message) - SMTP submission
    SYSPROC_IO_SECRET_GET,        // lambda.io.secret get(service, account) - stored text or null
    SYSPROC_IO_SECRET_SET,        // lambda.io.secret set(service, account, value)
    SYSPROC_IO_SECRET_DELETE,     // lambda.io.secret delete(service, account) - true if one was removed
    SYSFUNC_HASH,                 // hash(value, algorithm) - SHA-256/384/512 digest as binary
    SYSFUNC_VERIFY_SIGNATURE,    // verify_signature(public_key, data, signature, scheme)
    SYSPROC_UUID,                 // uuid() - a random version 4 UUID
    SYSFUNC_URL_ENCODE,           // url_encode(text) - RFC 3986 percent-encoding
    SYSPROC_IO_SQLITE_DATABASE,
    SYSPROC_IO_SQLITE_EXISTING,
    SYSPROC_IO_SQLITE_EXECUTE,
    SYSPROC_IO_SQLITE_QUERY,
    SYSPROC_IO_LOOPBACK_LISTEN,   // lambda.io.loopback listen() - a listener on 127.0.0.1
    SYSPROC_IO_LOOPBACK_ACCEPT,   // lambda.io.loopback accept(listener, seconds, page) - one browser request
    SYSPROC_IO_WEBSOCKET_OPEN,    // lambda.io.websocket: task-owned WS/WSS transport
    SYSPROC_IO_WEBSOCKET_SEND,
    SYSPROC_IO_WEBSOCKET_RECEIVE,
    SYSPROC_IO_WEBSOCKET_CLOSE,
    SYSPROC_IO_WEBSOCKET_LISTEN,
    SYSPROC_IO_WEBSOCKET_ACCEPT,
    SYSFUNC_WFC,                 // math.wfc(key, weights, masks, control) - exact batch solver
    SYSFUNC_HYDRAULIC,           // math.hydraulic(key, field, control, settings)
    SYSFUNC_DELAUNAY,            // math.delaunay(points, unique_ids, ccw_seed)
    SYSFUNC_RNG_KEYS,            // math.rng_keys(key, paths) - keyed path batch
    SYSFUNC_RNG_UNIFORM,         // math.rng_uniform(keys, counters) - row-major draws
    SYSFUNC_TRANSFORM_BUFFER,    // math.transform_buffer(matrix, positions, normals)
    SYSFUNC_POLYGON_DISTANCES,   // math.polygon_distances(points, outline)
    SYSPROC_IO_SQLITE_CREATE,   // lambda.io.sqlite create(path) - create-only target
    SYSPROC_IO_SQLITE_CLONE,    // lambda.io.sqlite clone(source, path) - snapshot target
    SYSPROC_IO_SQLITE_PREIMAGE, // lambda.io.sqlite preimage(tx, path) - pre-write backup
    SYSPROC_IO_CREATE_FILE,      // io.create_file(path, bytes) - publish a new file
    SYSPROC_IO_RDB_DATABASE,     // scoped, read-only RDB target
    SYSPROC_IO_RDB_SCHEMA,       // positional schema metadata
    SYSPROC_IO_RDB_DETAILS,      // index and foreign-key metadata
    SYSPROC_IO_RDB_EXECUTE,
    SYSPROC_IO_RDB_FETCH,
    SYSPROC_IO_RDB_FINISH,
    SYSPROC_IO_RDB_SPOOL_BEGIN,
    SYSPROC_IO_RDB_SPOOL_READ,
    SYSPROC_IO_RDB_SPOOL_CLOSE,
    SYSPROC_IO_RDB_SPOOL_SEAL,
    SYSPROC_IO_RDB_EXPORT_BEGIN,
    SYSPROC_IO_RDB_EXPORT_WRITE,
    SYSPROC_IO_RDB_EXPORT_COMMIT,
    SYSPROC_IO_RDB_EXPORT_ABORT,
    SYSPROC_IO_SQLITE_SNAPSHOT_DIGEST, // lambda.io.sqlite snapshot_digest(tx) - bounded read snapshot
} SysFunc;

#ifdef __cplusplus
// Runtime system-function type descriptor.
typedef struct TypeSysFunc : Type {
    SysFunc* fn;
} TypeSysFunc;
#endif


#ifdef __cplusplus
extern "C" {
#endif
Range* range();

List* list();  // constructs an empty list
void list_push(List *list, Item item);
void list_push_spread(List *list, Item item);  // push item, spreading if spreadable array
Item list_end(List *list);  // value finish: none is null, one is the item (S2.5.5v2)
Item list_end_item(List *list);  // item-position finish: none is a skip marker
Item list_collapse_value(Item value);  // collapse a finished for-expression result
Item list_collapse_item(Item value);   // same, in an item position
#ifdef __cplusplus
extern "C" {
#endif
bool item_is_list(Item value);  // the list kind bit, on a generic or packed array (S2.5.1v2)
// S2.5.6: a single-value slot stores a list as its array image (a one-level
// copy with the kind bit clear); any other value is returned unchanged.
Item slot_image(Item value);
// Finish an operator result in the kind its operands decide (S2.5.7v2): a list
// collapses (none is null, one is its item, S2.5.5v2); an array keeps its length.
Item seq_finish_kind(Item result, bool as_list);
// S2.5.7v2: a list is built, never computed -- a function or pipe result is an
// array for any sequence input, so a kind bit the result carries is cleared.
Item seq_finish_array(Item result);
// S2.5.7v2: an operator over two sequences is a list only when every sequence
// operand is a list; scalars and null do not decide.
bool seq_operands_are_lists(Item left, Item right);
#ifdef __cplusplus
}
#endif

// Spreadable array functions for for-expression results
Array* array_plain();  // constructs a plain empty array (no frame management)
#ifdef __cplusplus
extern "C" {
#endif
bool array_reserve_append_slots(Array* array, int64_t append_count);
bool js_array_has_props(const Array* arr);
Map* js_array_props(const Array* arr);
int64_t container_dense_capacity(const Array* arr);
void js_elements_set_props(Array* arr, Map* props);
void lambda_module_var_store(void* module_state, uint32_t slot, Item item);
// internal traced binding homes; their wrapper never enters a language value.
Array* lambda_capture_home_new(Item value);
Item lambda_capture_home_read(Array* home);
void lambda_capture_home_store(Array* home, Item value);
Item lambda_item_adopt_scalar_home(Item item, uint64_t* home);
// v3 (RV5): resolve a shape-2 call result whose lane 1 came back pending.
// Boxes the lane-2 payload into the CALLING frame's number extent.
Item lambda_item_resolve_pending(Item pending, uint64_t payload);
// v3 (RV12): same, reading lane 2 from `Context::mir_companion_slot`. This is
// how C callers of a boxed Lambda entry resolve; a resolved Item passes
// through untouched, so no caller-side test is required.
Item lambda_item_resolve_pending_slot(Item value);
int64_t lambda_restore_number_frame_top(uint64_t* top);
// A terminal native consumer owns this word only until it unboxes, discards, or
// copies the result into destination-owned storage; never return its Item.
#define LAMBDA_SCALAR_HOME(name) uint64_t name = 0
#ifdef __cplusplus
}
#endif
void array_drop_inplace(Array* arr, int64_t n);  // drop first n items in-place
void array_limit_inplace(Array* arr, int64_t n);  // limit to first n items in-place
void array_limit_last_inplace(Array* arr, int64_t n);  // limit to last n items in-place
// A for-expression's window clauses trim its raw stream in place, ahead of its
// one list finish (S2.5.2v2); counts follow take/drop (C15). Returns the
// stream, or an error for an invalid count.
#define FOR_WINDOW_OFFSET 1
#define FOR_WINDOW_LIMIT 2
#define FOR_WINDOW_LIMIT_LAST 4
Item for_window(Array* out, Item offset, Item limit, int64_t flags);
Array* array_spreadable();  // constructs a spreadable empty array
// Box an inferred pointer lane's words in place and drop the lane. An inferred
// lane is a representation choice, not a source contract (D4.3.1, D4.3.3),
// so any consumer that reads `items[]` directly must widen first. Returns false
// when the array has no inferred pointer lane to widen.
bool array_widen_inferred_pointer_lane(Array* array);
void array_push(Array* arr, Item item);  // sequence append: splices a list (D3.3.1)
void array_push_verbatim(Array* arr, Item item);  // verbatim append: one value, one item (D3.3.1)
// S9.3.1 capturing append for Lambda literals/comprehensions; array_push does not capture.
void array_push_capture(Array* arr, Item item);
// S9.3.1 (LR12-47): natives placing their arguments' items into a result.
void array_push_verbatim_capture(Array* arr, Item item);
void array_capture_items(Array* arr, int64_t start, int64_t end);
void array_push_spread(Array* arr, Item item);      // push item, spreading if spreadable array
// S12.3.5v2: `*x` is the list of x's items -- a sequence's items (a range
// materialized), nothing for null, any other value as one item -- collapsed
// for a value position or for an item position (S2.5.5v2)
Item seq_spread_value(Item item);
Item seq_spread_item(Item item);
Item array_end(Array* arr);  // finalize and return array as Item
Item array_end_flat(Array* arr);  // pack scalar lanes while retaining nested row owners
#ifdef __cplusplus
extern "C" {
#endif
uint64_t lambda_item_hash(Item key, uint64_t seed0, uint64_t seed1);
int lambda_item_compare(Item a, Item b);
Array* fn_group_by_keys(Item rows_item, Item keys_item, int64_t key_count);  // records [members, key...] (S14.1.1v2)
Array* fn_join_seed_tuples(Item rows_item, Item name_item, Item idx_name_item, Item idx_vals_item);
Array* fn_hash_join_tuples(Item prior_tuples_item, Item prior_keys_item, Item rows_item,
    Item row_keys_item, Item name_item, int64_t optional, Item idx_name_item, Item idx_vals_item);
Array* fn_cross_join_tuples(Item prior_tuples_item, Item rows_item, Item name_item,
    Item idx_name_item, Item idx_vals_item);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Dynamic function invocation for first-class functions
Item fn_call(Function* fn, List* args);
Item fn_call0(Function* fn);
Item fn_call1(Function* fn, Item a);
Item fn_call2(Function* fn, Item a, Item b);
Item fn_call3(Function* fn, Item a, Item b, Item c);
// Hosted-language result-boundary forms. First-party Lambda/LambdaJS v3
// entries resolve their companion lane in Context; hosted callbacks may still
// supply an explicit payload owner across this C boundary.
Item fn_call_into(Function* fn, List* args, uint64_t* result_home);
Item fn_call_borrowed_into(Function* fn, List* args, uint64_t* result_home);  // caller published CW33 var homes
// S12.3.4 dynamic application; colour-split for the error convention.
// Named *_apply_args, not *_call: `fn_call` is already the dynamic dispatcher.
Item fn_apply_args(Item callee, Item args);
Item pn_apply_args(Item callee, Item args);
Item fn_call1_into(Function* fn, Item a, uint64_t* result_home);
Item fn_call2_into(Function* fn, Item a, Item b, uint64_t* result_home);
Item fn_call3_into(Function* fn, Item a, Item b, Item c, uint64_t* result_home);


#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// System function: exists() - check if file/directory exists (called by JIT)
#ifdef __cplusplus
extern "C"
#endif
Bool fn_exists(Item path);

// Create function wrappers for first-class usage
Function* to_fn(fn_ptr ptr);
Function* to_fn_n(fn_ptr ptr, int arity);
Function* to_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_sys_fn_named(fn_ptr ptr, int arity, const char* name);
Function* to_closure(fn_ptr ptr, int arity, void* env);
Function* to_closure_named(fn_ptr ptr, int arity, void* env, const char* name);
#ifdef __cplusplus
extern "C" {
#endif
void lambda_function_mark_mir_context_abi(Function* fn);
void lambda_function_mark_lambda_boxed_function(Function* fn);
void lambda_function_mark_lambda_boxed_procedure(Function* fn);
void lambda_function_mark_mir_public_return_shape(Function* fn, uint32_t shape);
void lambda_function_set_type(Function* fn, void* fn_type);
#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
}
#endif

#include "runtime/gc_scope.h"

#ifdef __cplusplus
extern "C" {
#endif
void* heap_calloc(size_t size, TypeId type_id);
// Closure environments use scalar side storage so captured wide numbers remain
// valid after their creating invocation's number frame is reclaimed.
void* heap_calloc_closure_env(size_t size);
void* heap_calloc_class(size_t size, TypeId type_id, int cls);  // allocate with pre-computed size class
void* heap_data_calloc(size_t size);  // allocate GC-managed data buffer (for map/object data)
uint64_t* heap_gc_root_slot_new(uint64_t value);
bool heap_try_register_gc_root(uint64_t* slot);
void heap_unregister_gc_root(uint64_t* slot);
bool heap_try_register_gc_root_range(uint64_t* base, int count);
void heap_no_gc_scope_begin(void);
void heap_no_gc_scope_end(void);
void heap_gc_defer_collection_begin(void);
void heap_gc_defer_collection_end(void);
LambdaGcScopeCheckpoint lambda_gc_scope_checkpoint_capture(void);
bool lambda_gc_scope_checkpoint_restore(const LambdaGcScopeCheckpoint* checkpoint);
// String creation for name pooling
LAMBDA_RT_API String* heap_create_name(const char* name);
// String creation for runtime strings
String* heap_strcpy(const char* src, int64_t len);
Binary* heap_binary_from_bytes(const char* src, int64_t len);
Binary* heap_binary_from_storage(ByteStorage* storage, size_t offset,
    size_t length, bool is_ascii);
Binary* heap_binary_slice(Binary* source, size_t offset, size_t length);
Binary* heap_binary_copy(Binary* source);
Binary* heap_binary_concat(Binary* left, Binary* right);
// Symbol creation for runtime symbols
Symbol* heap_create_symbol(const char* symbol, size_t len);
// S2.2.2v2: `symbol.empty`, the one zero-length symbol; static, never GC-managed
Symbol* symbol_empty(void);
// a map/attribute name as an iteration key: the empty name is `symbol.empty`
Symbol* name_key_symbol(const char* name, size_t len);
Symbol* name_key_symbol_ns(const char* name, size_t len, struct Target* ns);
#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
extern "C" {
#endif
Array* array_fill(Array* arr, int count, ...);
ArrayNum* array_int_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_int64_fill(ArrayNum* arr, int count, ...);
ArrayNum* array_float_fill(ArrayNum* arr, int count, ...);

typedef struct Map Map;
Map* map_fill(Map* map, ...);
// Same fill from a caller-rooted Item span; the T0 walker has no varargs.
Map* map_fill_items(Map* map, const Item* values, int value_count);

typedef struct Element Element;
Element* elmt_fill(Element *elmt, ...);
// Same fill from a caller-rooted Item span; the T0 walker has no varargs.
Element* elmt_fill_items(Element *elmt, const Item* values, int value_count);
// Retain a field written to an arena-backed UI element across GC cycles.
bool ui_prepare_element_field(Item* value);


#ifdef __cplusplus
}
#endif

struct TypeMap;
struct TypeElmt;
struct TypeObject;
struct Context {
    Pool* pool;
    Arena* arena;  // arena allocator (for input parsing path; also result arena in ui_mode)
    void** consts;
    void* type_list;  // type definitions list (ArrayList* at runtime, void* for JIT access)
    Url* cwd;  // current working directory
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main; // whether to run main procedure on start
    uintptr_t stack_limit; // recoverable native stack limit of the executing thread (JC23)
    bool ui_mode; // allocate fat DomElement/DomText on arena for unified DOM tree
    uint64_t* side_root_base;
    uint64_t* side_root_top;
    uint64_t* side_root_commit_limit;
    uint64_t* side_root_limit;
    uint64_t* side_number_base;
    uint64_t* side_number_top;
    uint64_t* side_number_commit_limit;
    uint64_t* side_number_limit;
    uint64_t mir_return_lane;
    // Scratch cell for JIT double<->bits reinterpretation. MIR has no reg<->reg
    // bitcast, so the pair is a typed store + differently-typed load; routing it
    // through the Context the frame already holds costs no per-function stack
    // setup. Never GC-scanned — raw double bits in a scanned slot would be read
    // as a tagged Item. Single-thread-owned by the same invariant as the
    // side-stack pointers above, and dead between the store and its load.
    uint64_t mir_bitcast_scratch;
    // Return-value convention v3, RV12: lane 2 as a *location* rather than a
    // register. A C prototype has no portable spelling for MIR's two-result
    // convention, so every entry reachable from C — the public `_b` wrappers
    // called through the boxed-call trampolines and `fn->invoke` — hands its
    // wide payload back here instead, with lane 1 carrying the pending Item.
    // The protocol is otherwise byte-identical to the register form: single
    // live value, dead at the next call, resolved at the caller's first
    // resolution point.
    //
    // Same contract as the two cells above: never GC-scanned (raw payload
    // bits, never a pointer — RV8), single-thread-owned, and dead outside the
    // window between a shape-2 return and its resolution.
    uint64_t mir_companion_slot;
    // CW33 M1a (COW §11.10): per-call transport for `var`-parameter HOME
    // addresses -- the address of the caller binding's GC root slot. The
    // caller writes the cells for the callee's untyped `var` positions
    // immediately before a direct call (an address, or 0 when the argument
    // has no boxed-classed home); the callee prologue consumes them into its
    // own registers and ZEROES the cells, so they are always 0 outside the
    // call window. Same contract as mir_companion_slot: never GC-scanned
    // (raw addresses of already-rooted slots), single-thread-owned, dead
    // outside the transport window. Deleted when the true pointer ABI lands.
    uint64_t* mir_var_homes[LAMBDA_MAX_FUNCTION_ARGS];
};

// A property key specification is compiler-neutral data stored in the sealed
// MIR module image. `name_offset` is relative to the beginning of the
// specification array and is valid only for an ordinary STRING key.
typedef struct PropertyKeySpec {
    uint32_t predefined_id;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t reserved;
} PropertyKeySpec;

// Sealed layouts distinguish a cache-wide logical unit from a runtime-local
// dense slab with this bit. Logical compilation-unit allocation reserves it.
#define LAMBDA_MODULE_ID_LOGICAL_UNIT_FLAG ((uint32_t)1u << 31)

// Immutable BSS metadata for one sealed MIR module. `module_id` is either a
// dense slab ID or a logical compilation unit marked by the flag above; each
// Runtime resolves the latter to a dense local slab. Mutable bindings remain
// per EvalContext (D8.5.1).
typedef struct LambdaModuleLayout {
    uint32_t module_id;
    uint32_t var_count;
    uint32_t property_key_count;
    uint32_t property_key_bytes_size;
    uint32_t reserved;
    // The one link-time cell: where a satellite's key suffix sits in its
    // owner's key table. Publication writes it; the generated code reads it.
    uint32_t property_key_base;
    const PropertyKeySpec* property_key_specs;
} LambdaModuleLayout;

// A satellite contributes a sealed suffix to its owner's key table. The image
// is compiled without the receiving runtime's state (D8.3.5), and other
// satellites may publish first, so the suffix is placed only when publication
// links it; `property_key_base` records where (D5.6.2).
#define LAMBDA_MODULE_LAYOUT_APPEND_PROPERTY_KEYS 0x80000000u

typedef struct LambdaModuleVarRef {
    // Uses the same physical-or-flagged-logical encoding as LambdaModuleLayout.
    uint32_t module_id;
    uint32_t slot;
} LambdaModuleVarRef;



#ifdef __cplusplus
extern "C" {
#endif
    Array* array();
    ArrayNum* array_int();
    ArrayNum* array_int64();
    ArrayNum* array_float();

    ArrayNum* array_num_new(ArrayNumElemType elem_type, int64_t length);
    ArrayNum* array_num_new_uninit(ArrayNumElemType elem_type, int64_t length);  // lanes unset: caller writes all before read/GC
    ArrayNum* array_num_new_with_extra(ArrayNumElemType elem_type, int64_t length,
                                       int64_t extra);
    ArrayNum* array_num_new_external_view(Container* base, void* data_base,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    ArrayNum* array_num_new_buffer_view(Container* base, ByteBufferHandle* handle,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    ArrayNum* array_num_new_storage_view(ByteStorage* storage,
        ArrayNumElemType elem_type, int64_t byte_offset, int64_t length, bool mutable_view);
    bool array_num_init_derived_view(ArrayNum* view, ArrayNumShape* shape,
        ArrayNum* source, int64_t relative_elem_offset);
    void* array_num_resolve_data(ArrayNum* array, bool write);
    ArrayNum* array_int_new(int64_t length);
    ArrayNum* array_int64_new(int64_t length);
    ArrayNum* array_float_new(int64_t length);

    void array_float_set(ArrayNum *arr, int64_t index, double value);
    void array_int_set(ArrayNum *arr, int64_t index, int64_t lane);
    // Grow an owned one-dimensional ArrayNum and return its rooted current
    // header. Views and N-D carriers intentionally cannot grow in place.
    ArrayNum* array_num_reserve_capacity(ArrayNum *arr, int64_t minimum_capacity);
    // Exact-width store for a value that has already crossed the destination
    // element contract. Unlike array_num_set_item(), this never coerces.
    bool array_num_store_admitted(ArrayNum *arr, int64_t index, Item value);
    bool array_num_admits_value(ArrayNum *arr, Item value);  // exactly the lane type
    void array_num_set_item(ArrayNum *arr, int64_t index, Item value);
    Item array_num_read_item(ArrayNum *arr, int64_t index);
    double array_num_read_double(ArrayNum *arr, int64_t index);
    double array_num_get_number_value(ArrayNum *arr, int64_t index);
    void array_num_set_int64_value(ArrayNum *arr, int64_t index, int64_t value);
    void array_num_set_double_value(ArrayNum *arr, int64_t index, double value);
    bool array_num_copy_same_type_bytes(ArrayNum *dst, int64_t dst_index,
        ArrayNum *src, int64_t src_index, int64_t count);
    bool array_num_copy_equal_size_bytes(ArrayNum *dst, int64_t dst_index,
        ArrayNum *src, int64_t src_index, int64_t count);
    bool array_num_reverse_bytes(ArrayNum *arr);
    bool array_num_copy_reversed_bytes(ArrayNum *dst, ArrayNum *src);

    Map* map(int64_t type_index);
    Map* map_with_data(int64_t type_index);
    Map* map_fill_reserve_data(Map* map);
    // shared physical construction; profiles retain layout and property admission.
    Map* map_alloc_for_type(struct TypeMap* map_type, int64_t minimum_capacity);
    bool map_field_store(void* field_ptr, Item value, TypeId value_type);
    void map_field_store_int_as_float(void* field_ptr, Item value);
    // neutral packed-field mutation; no descriptors, prototype dispatch, or JS hooks.
    bool map_shape_set(Map* map, String* key, Item value);
    // compiler-proven ordinary string keys bypass Symbol/private-name admission.
    bool map_shape_set_text(Map* map, String* key, Item value);
    // field must be resolved against the current shape; NULL means an absent own field.
    bool map_shape_set_resolved(Map* map, String* key, struct ShapeEntry* field, Item value,
        bool preserve_storage);
    bool map_shape_delete(Map* map, String* key);
    bool map_shape_delete_text(Map* map, String* key);
    bool map_shape_delete_resolved(Map* map, struct ShapeEntry* field);
    Map* map_with_tl(int64_t type_index, void* type_list_ptr);
    Map* map_with_type_tl(struct TypeMap* map_type, void* type_list_ptr);
    Element* elmt(int64_t type_index);
    Element* elmt_with_tl(int64_t type_index, void* type_list_ptr);
    Element* elmt_with_type(struct TypeElmt* elmt_type);
    void elmt_content_begin(Element* element);
    Item output_content_append(List* destination, Item value);
    uint64_t output_content_checkpoint(List* destination);
    void output_content_rewind(List* destination, uint64_t size);
    Bool output_content_is_virtual(List* destination);
    Item fn_call_emit(Function* fn, List* args, List* destination);
    Object* object(int64_t type_index);
    Object* object_with_data(int64_t type_index);
    Object* object_with_tl(int64_t type_index, void* type_list_ptr);
    Item object_from_fields(const struct TypeObject* type, Item fields);
    // S2.1.3 object content face (lazily allocated side list)
    struct List* object_content(Object* obj);
    Object* object_content_fill(Object* obj, int count, ...);
    Object* object_content_fill_items(Object* obj, const Item* values, int count);
    // Same fill from a caller-rooted Item span; the T0 walker has no varargs.
    Object* object_fill_items(Object* obj, const Item* values, int value_count);
    // S11.4.10: admit a literal's deferred fields from a caller-rooted span;
    // return the failure's error, or ItemNull when every field is admitted.
    Item object_literal_admit_fields(const struct TypeObject* type, Item* values, int count,
        uint64_t deferred);

    // these getters use the runtime number side stack
    Item array_get(Array *array, int64_t index);
    Item array_num_get(ArrayNum *array, int64_t index);
    Item array_num_get_borrowed(ArrayNum* array, int64_t index);
    Item array_num_snapshot(ArrayNum* array);
    Item array_int_get(ArrayNum *array, int64_t index);
    Item array_int64_get(ArrayNum* array, int64_t index);
    Item array_float_get(ArrayNum* array, int64_t index);
    // fast-path getters: return native types, skip boxing
    int64_t array_int_get_raw(ArrayNum *array, int64_t index);
    int64_t array_int64_get_raw(ArrayNum *array, int64_t index);
    double array_float_get_value(ArrayNum *arr, int64_t index);
    Item list_get(List *list, int64_t index);
    Item fn_string_ascii_at(Item str, int64_t index);
    String* get_ascii_char_string(unsigned char ch);
    Item fn_string_ord_at(Item str, int64_t index);
    uint64_t fn_string_char_word(Item str, int64_t index);
    Item map_get(Map* map, Item key);
    Item elmt_get(Element *elmt, Item key);
    Item object_get(Object* obj, Item key);
    void object_type_set_method(int64_t type_index, const char* method_name,
                                fn_ptr func_ptr, int64_t arity, int64_t is_proc);
    void object_type_set_constraint(int64_t type_index, fn_ptr constraint_func);
    Item item_at(Item data, int64_t index);
    Item item_attr(Item data, const char* key);  // get attribute by name
    bool path_is_property_name(const char* key);
    Item path_property_get(Path* path, const char* key);  // built-in Path properties (shared by fn_member/item_attr)
    SymbolKeyList* item_keys(Item data);     // get typed list of Symbol* attribute names
    // Distinct-key field count of map-shaped storage (flattens spread slots,
    // dedupes duplicate names) — the count for-iteration performs (S8.3.1).
    int64_t map_flat_field_count(struct TypeMap* map_type, void* map_data);
    SymbolKeyList* symbol_key_list_new(int64_t initial_capacity);
    bool symbol_key_list_append(SymbolKeyList* keys_ptr, Symbol* symbol);
    int64_t symbol_key_list_len(void* keys_ptr);
    Symbol* symbol_key_list_at(void* keys_ptr, int64_t index);
    void symbol_key_list_free(void* keys_ptr);

    // Unified for-loop iteration helpers (key_filter: 0=ALL, 1=INT, 2=SYMBOL)
    int64_t iter_len(Item data, void* keys_ptr, int key_filter);
    Item iter_key_at(Item data, void* keys_ptr, int64_t idx, int key_filter);
    Item iter_val_at(Item data, void* keys_ptr, int64_t idx, int key_filter);

    Bool is_truthy(Item item);
    Item v2it(List *list);

    Item flt2it(double dval);  // canonical double -> Item encoder
    Item int2it(double value);     // integral double -> int Item (boundary form)
    Item int2it_lane(int64_t lane); // LANE value -> int Item (v5 canonical encoder)
    double lambda_int_lane_to_double_c(int64_t lane);
    double lambda_float_null_lane_c(void);
    int64_t lambda_double_to_int_lane_c(double value);
    int64_t lambda_item_to_int_lane_c(uint64_t item_bits);
    int64_t lambda_int_lane_add_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_sub_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_mul_slow(int64_t a, int64_t b);
    int64_t lambda_int_lane_divmod_slow(int64_t a, int64_t b, int64_t is_mod);
    Item int2it_i64(int64_t value); // same encoder, native-int64 caller
    Item push_d(double dval);
    Item box_int64_value(int64_t lval);
    Item box_uint64_value(uint64_t uval);
    Item push_k(DateTime dtval);
    Item push_c(int64_t cval);

    // Const pool pointer — modules override this single macro to redirect to module-local consts
    #define _const_pool  rt->consts

    #define const_d2it(index)    lambda_float_ptr_to_item((const double*)_const_pool[index])
    #define const_l2it(index)    l2it(_const_pool[index])
    #define const_c2it(index)    c2it(_const_pool[index])
    #define const_s2it(index)    s2it(_const_pool[index])
    #define const_y2it(index)    y2it(_const_pool[index])
    #define const_k2it(index)    push_k(const_k(index))
    #define const_x2it(index)    x2it(_const_pool[index])

    #define const_s(index)      ((String*)_const_pool[index])
    #define const_x(index)      ((Binary*)_const_pool[index])
    #define const_c(index)      ((Decimal*)_const_pool[index])
    #define const_k(index)      (*(DateTime*)_const_pool[index])

    // item unboxing
    Item coerce_num_sized(Item value, int64_t num_type);
    Item coerce_uint64(Item value);

    // generic field access function
    Item fn_index(Item item, Item index);
    Item fn_index_nd(Item item, int ndim, int64_t* indices);   // item[i, j, k]: null unless an N-D array
    Item fn_index_set(Item item, Item index, Item value);
    int64_t fn_int64_index(Item item);
    Item fn_member(Item item, Item key);
    // S2.4.2v5: one key step of a path -- an IntKey or NameKey through
    // S8.2.1v4, never a property read; null when the key names nothing.
    Item fn_path_key(Item path, Item key);
    // One static step (LPathSegmentType) on a computed path; null stays null.
    Item fn_path_step(Item path, int64_t step_type, const char* name, int64_t int_value);
    // S12.3.3v2: object member access (key domain, then the type's method
    // chain), shared by the ANY and static member lanes.
    Item lambda_object_member(Item self, const char* key);
    const struct TypeMethod* lambda_object_find_method(const struct TypeObject* type,
        const char* key);
    Item fn_member_by_id(Item item, uint32_t name_id);
    // length function
    int64_t fn_len(Item item);
    Item fn_len_checked(Item item);
    int64_t fn_count(Item item);  // S8.3.3v3: the size of the run an item is
    Item fn_content(Item item);   // read-only array view over an element's content
    int64_t fn_seq_count(Item item);  // positions a positional traversal visits
    Item fn_int(Item a);
    Item fn_int64(Item a);
    Item fn_float(Item a);
    Item fn_decimal(Item a);
    Item fn_binary(Item a);
    Item fn_binary_decode(Item binary, Item schema);
    Item fn_complex1(Item a);
    Item fn_complex2(Item real, Item imag);
    Item fn_real(Item a);
    Item fn_imag(Item a);
    Item fn_conj(Item a);
    Item complex_new(double real, double imag);
    Item fn_complex_sqrt(Item a);
    Item fn_complex_log(Item a);
    Item fn_complex_exp(Item a);
    Item fn_complex_sin(Item a);
    Item fn_complex_cos(Item a);
    Item fn_complex_tan(Item a);

    Item fn_add(Item a, Item b);
    Item fn_mul(Item a, Item b);
    Item fn_sub(Item a, Item b);
    Item fn_div(Item a, Item b);
    Item fn_idiv(Item a, Item b);
    Item fn_pow(Item a, Item b);
    Item fn_mod(Item a, Item b);
    Item fn_abs(Item a);
    Item fn_round(Item a);
    Item fn_floor(Item a);
    Item fn_ceil(Item a);
    Item fn_min1(Item a);
    Item fn_min2(Item a, Item b);
    Item fn_max1(Item a);
    Item fn_max2(Item a, Item b);
    Item fn_sum(Item a);
    Item fn_avg(Item a);
    Item fn_avg_skip_null(Item a, bool skip_null);
    Item fn_union(Item a, Item b);      // S10.1.1v2: `|`, `&`, `!` build a type; the value set
    Item fn_intersect(Item a, Item b);  // functions are unique/intersect/except below
    Item fn_exclude(Item a, Item b);
    Item fn_unique2(Item a, Item b);
    Item fn_unique3(Item a, Item b, Item c);
    Item fn_unique4(Item a, Item b, Item c, Item d);
    Item fn_intersect2(Item a, Item b);
    Item fn_intersect3(Item a, Item b, Item c);
    Item fn_intersect4(Item a, Item b, Item c, Item d);
    Item fn_except(Item a, Item b);
    Item fn_pos(Item a);
    Item fn_neg(Item a);

    // truthy idioms
    Item fn_and(Item a, Item b);
    Item fn_or(Item a, Item b);
    Item op_and(Bool a, Bool b);
    Item op_or(Bool a, Bool b);

    Bool fn_eq(Item a, Item b);
    // language operators retain the original error; Bool adapters serve native predicates
    Item fn_eq_value(Item a, Item b);
    Item fn_ne_value(Item a, Item b);
    Item fn_not_value(Item a);
    Item fn_ref_eq_value(Item a, Item b);
    Bool fn_eq_strict(Item a, Item b);
    Bool fn_ne(Item a, Item b);
    Bool fn_str_eq_ptr(String* a, String* b);
    Bool fn_sym_eq_ptr(Symbol* a, Symbol* b);
    // Ordered comparisons return Item (Any): a boolean mask when an operand is an
    // ARRAY_NUM (vectorized), else a boxed scalar bool. The *_scalar helpers expose
    // the raw 3-state scalar comparison for callers that only compare scalars.
    Bool fn_lt_scalar(Item a, Item b);
    Bool fn_gt_scalar(Item a, Item b);
    Bool fn_le_scalar(Item a, Item b);
    Bool fn_ge_scalar(Item a, Item b);
    int total_cmp(Item a, Item b);
    Item fn_lt(Item a, Item b);
    Item fn_gt(Item a, Item b);
    Item fn_le(Item a, Item b);
    Item fn_ge(Item a, Item b);
    Bool fn_is(Item a, Item b);
    Bool fn_subtype(Item a, Item b);
    // Type-boundary primitives used by the MIR emitter. `lambda_type_check`
    // returns its input on success and a diagnostic-carrying Error Item on a
    // mismatch; no native lane may be entered before this succeeds.
    bool lambda_type_matches(Item value, Type* expected);
    // Pure content replacement retains the qualified tag and attribute contract.
    Item lambda_element_with_content(Item element, Item children);
    Item lambda_element_present_content(Item element, Item children);
    // S11.1.3: membership in a range type, and its bounds as one inclusive
    // interval (codepoints for a character range). A bound outside the
    // range's domain makes both answer false: that range admits nothing.
    bool lambda_range_type_contains(const Type* range_type, Item value);
    bool lambda_range_type_bounds(const Type* range_type, int64_t* start, int64_t* end);
    // where `value` sits in the range's domain, on the scale of its bounds:
    // an exact integer, or a one-codepoint string's codepoint
    bool lambda_range_type_position(const Type* range_type, Item value, int64_t* position);
    Item lambda_type_error(Item actual, Type* expected, const char* boundary);
    Item lambda_type_check(Item value, Type* expected, const char* boundary);
    // S12.1.4v2(3): the run-time half of an `fn`-context call's colour check.
    // Both return ItemNull / the argument on success and an error Item when
    // the call would be a `pn` call that `fn` context cannot make.
    Item lambda_fn_colour_guard(Function* fn, const Item* args, int argc,
        uint32_t guard);
    Item lambda_fn_colour_arg_check(Item argument);
    Item lambda_fn_colour_guard_list(Item callee, List* args, uint32_t guard);
    // Env-aware form used only by binder-carrying function entries.  The
    // legacy ABI remains a NULL-env wrapper for every existing boundary.
    Item lambda_type_check_env(Item value, Type* expected, Type** env,
        const char* boundary);
    // status separates an admitted Error value from a failed dependent check.
    Item lambda_type_check_env_status(Item value, Type* expected, Type** env,
        const char* boundary, Bool* admitted);
    // A boundary label that is formatted only if the check fails. The T0
    // walker checks every declared binding, assignment and argument, and
    // formatting the label eagerly cost it 15-29% of its time. `text` is used
    // as-is when `format` is NULL; `subject`/`owner`/`index` belong to `format`.
    typedef struct LambdaBoundary LambdaBoundary;
    typedef void (*LambdaBoundaryFormat)(const LambdaBoundary* boundary,
        char* out, size_t capacity);
    struct LambdaBoundary {
        const char* text;
        LambdaBoundaryFormat format;
        const void* subject;
        const void* owner;
        int32_t index;
        // Resolve the source point only on failure; successful checks stay lazy.
        const char* source;
        const char* file;
        uint32_t source_offset;
    };
    Item lambda_type_check_lazy(Item value, Type* expected, Type** env,
        const LambdaBoundary* boundary);
    Item lambda_report_boundary_failure(Item failure, const char* binding,
        int64_t tail_count, int64_t store);
    // Exact semantic-type guard for a value binder. Container shapes compare
    // their authoritative descriptor pointer; generic containers compare the
    // same normalized kind reported by type(value).
    Bool lambda_value_type_is_exact(Item value, Type* expected);
    // S11.4.8v2: a value binder records the admitted value's narrowest type.
    Type* lambda_binder_value_type(Item value);
    // Box a selected contract as a first-class `type` value. Binder frames
    // retain the inner contract for admission; expression reads need this
    // self-tagged wrapper (D4.1.1).
    Item lambda_type_value_from_contract(Type* contract);
    // Admit an exact primitive ArrayNum, or reify an empty ordinary Array,
    // under a rank-one primitive T[] contract and install its certificate.
    // Other carriers retain the complete checked-boundary path.
    Item lambda_fill_for_contract(Item count, Item value, Type* expected,
        const char* boundary);
    // activation-local metadata resolution; certificates belong to the current heap
    ArrayRepCert* lambda_array_rep_cert_resolve(Type* contract);
    Item lambda_fill_for_contract_int_lane_resolved(int64_t count, Item value,
        Type* expected, const char* boundary, ArrayRepCert* cert);
    // shared construction kernel; count and scalar lane are admitted by the caller
    Item lambda_fill_primitive(int64_t count, Item value, ArrayNumElemType lane);
    // native lane bits; a non-null certificate is already proven to match the scalar and count
    Item lambda_fill_primitive_bits(int64_t count, uint64_t bits, ArrayNumElemType lane,
        ArrayRepCert* cert);
    bool lambda_try_fill_for_contract(Item count, Item value, Type* expected, Item* result);
    Item lambda_array_admit_numeric_contract(Item value, Type* expected,
        const char* boundary);
    Item lambda_map_set_checked(Item owner, Item key, Item value, Type* expected,
        const char* boundary);
    Item lambda_map_set_checked_inplace(Item owner, Item key, Item value, Type* expected,
        const char* boundary);
    Item lambda_map_path_set_checked(Item owner, Item path, Item value, Type* expected,
        const char* boundary);
    // NM-O8 typed arm: validates in place instead of swapping a candidate in.
    Item lambda_map_path_set_checked_inplace(Item owner, Item path, Item value,
        Type* expected, const char* boundary);
    // T27-4: key-span twin of the two setters above; `keys` holds `count` keys
    // (1..LAMBDA_PATH_KEYS_MAX) in caller memory that need not be GC-visible;
    // shape = count | inplace<<8 | index-key bits <<16, leaf_contract resolved
    // by the compiler (or NULL). Allocates no path array on a proven root.
    Item lambda_map_path_set_checked_keys(Item owner, Item value, const Item* keys,
        int64_t shape, Type* expected, Type* leaf_contract);
    // one link of a declared path's contract walk; *open_leaf below an open array
    Type* lambda_map_path_contract_step(Type* current, Item key, bool* open_leaf);
    Item lambda_array_set_checked_item(Item owner, Item key, Item value, Type* expected,
        const char* boundary);
    // shared resolved store plan; primitive proofs still guard the live carrier.
    Item lambda_array_set_checked_preplanned(Item owner, Item key, Item value,
        Type* expected, Type* element, const LaneStorageDesc* lane,
        const char* boundary, bool inplace, bool uncounted_primitive);
    Item lambda_array_set_checked_inplace(Item owner, int64_t index, Item value, Type* expected,
        const char* boundary);
    // Nested array writes validate from the declared root, then relink private
    // children so a wrong leaf cannot weaken an enclosing T[][] contract.
    Item lambda_array_path_set_checked(Item owner, Item path, Item value, Type* expected,
        const char* boundary);
    Item lambda_array_path_set_checked_inplace(Item owner, Item path, Item value,
        Type* expected, const char* boundary);
    // Mask writes stage every selected value before touching the ArrayNum
    // payload, preserving the declared T[] contract transactionally.
    Item lambda_array_mask_assign_checked(Item owner, Item mask, Item value,
        Type* expected);
    Item lambda_array_mask_assign_checked_inplace(Item owner, Item mask, Item value,
        Type* expected);
    // Coordinate writes target a physical N-D ArrayNum but retain the
    // enclosing semantic T[] contract.
    Item lambda_array_set_nd_checked(Item owner, int ndim, int64_t* indices, Item value,
        Type* expected);
    Item lambda_array_set_nd_checked_inplace(Item owner, int ndim, int64_t* indices,
        Item value, Type* expected);
    // Typed append admits the element before capacity growth, then preserves
    // the array's full representation proof on the published replacement.
    Item lambda_array_push_checked(Item owner, Item value, Type* expected,
        const char* boundary);
    Item lambda_array_push_checked_inplace(Item owner, Item value, Type* expected,
        const char* boundary);
    Bool fn_is_nan(Item a);  // IEEE NaN check: expr is nan
    Bool fn_in(Item a, Item b);
    Bool fn_at(Item a, Item b);
    Bool fn_of(Item a, Item b);

    // query operations: search data for items matching a type
    Item fn_query(Item data, Item type_val, int direct);
    Item fn_step_items(Item data);    // S8.2.5: `e.*`, the run of e's items
    Item fn_step_closure(Item data);  // S8.2.5: `e.**`, the closure of `.*`
    Item fn_navigate_value(Item object, int root);  // `.~~`/`./` with no `~` context

    // vector arithmetic operations (element-wise)
    Item vec_add(Item a, Item b);
    Item vec_sub(Item a, Item b);
    Item vec_mul(Item a, Item b);
    Item vec_div(Item a, Item b);
    Item vec_mod(Item a, Item b);
    Item vec_pow(Item a, Item b);

    // whole-array typed reduction → double accumulator, correct for every element
    // type (the per-type fn_sum/min/max paths only handled INT/INT64/FLOAT).  op
    // codes match ReduceOp.  Contiguous arrays use the vectorized kernel.
    enum { ARRAY_RED_SUM = 0, ARRAY_RED_PROD = 1, ARRAY_RED_MIN = 2, ARRAY_RED_MAX = 3, ARRAY_RED_AVG = 4 };
    double array_num_reduce_double(ArrayNum* arr, int op);

    // vector system functions (math module)
    Item fn_math_prod(Item a);
    Item fn_math_cumsum(Item a);
    Item fn_math_cumprod(Item a);
    Item fn_argmin(Item a);
    Item fn_argmax(Item a);
    Item fn_fill(Item n, Item value);
    Item fn_math_dot(Item a, Item b);
    Item fn_math_norm(Item a);
    // statistical functions (math module)
    Item fn_math_mean(Item a);
    Item fn_math_mean_skip_null(Item a, bool skip_null);
    Item fn_math_median(Item a);
    Item fn_math_median_skip_null(Item a, bool skip_null);
    Item fn_math_variance(Item a);
    Item fn_math_variance_skip_null(Item a, bool skip_null);
    Item fn_math_deviation(Item a);
    Item fn_math_deviation_skip_null(Item a, bool skip_null);
    // element-wise math functions (math module)
    Item fn_math_sqrt(Item a);
    Item fn_math_log(Item a);
    Item fn_math_log10(Item a);
    Item fn_math_exp(Item a);
    Item fn_math_sin(Item a);
    Item fn_math_cos(Item a);
    Item fn_math_tan(Item a);
    // inverse trigonometric
    Item fn_math_asin(Item a);
    Item fn_math_acos(Item a);
    Item fn_math_atan(Item a);
    Item fn_math_atan2(Item a, Item b);
    // hyperbolic
    Item fn_math_sinh(Item a);
    Item fn_math_cosh(Item a);
    Item fn_math_tanh(Item a);
    // inverse hyperbolic
    Item fn_math_asinh(Item a);
    Item fn_math_acosh(Item a);
    Item fn_math_atanh(Item a);
    // exponential/logarithmic variants
    Item fn_math_exp2(Item a);
    Item fn_math_expm1(Item a);
    Item fn_math_log2(Item a);
    // power/root
    Item fn_math_pow(Item a, Item b);
    Item fn_math_cbrt(Item a);
    Item fn_trunc(Item a);
    Item fn_math_hypot(Item a, Item b);
    Item fn_math_log1p(Item a);
    Item fn_sign(Item a);
    // random number generation (pure functional, SplitMix64)
    Item fn_math_random(Item seed);

    // scientific kernels (math module); boxed boundaries preserve error results
    Item fn_math_erf(Item a);
    Item fn_math_erfc(Item a);
    Item fn_math_fft(Item a);
    Item fn_physics_simulate(Item scene, Item options);
    Item fn_physics_validate(Item scene);
    Item fn_physics_query(Item scene, Item queries);
    Item fn_navigation_path(Item source, Item start, Item goal, Item options);
    Item fn_navigation_validate(Item source);
    Item fn_math_ifft(Item a);
    Item fn_math_rfft(Item a);
    Item fn_math_irfft(Item a, Item length);
    Item fn_math_solve(Item a, Item b);
    Item fn_math_lstsq(Item a, Item b);
    Item fn_math_orient2d(Item a, Item b, Item c);
    Item fn_math_orient3d(Item a, Item b, Item c, Item d);
    Item fn_math_incircle(Item a, Item b, Item c, Item d);
    Item fn_math_wfc(Item key, Item weights, Item masks, Item control);
    Item fn_math_hydraulic(Item key, Item field, Item control, Item settings);
    Item fn_math_delaunay(Item points, Item ids, Item seed);
    Item fn_math_rng_keys(Item key, Item paths);
    Item fn_math_rng_uniform(Item keys, Item counters);
    Item fn_math_transform_buffer(Item matrix, Item positions, Item normals);
    Item fn_math_polygon_distances(Item points, Item outline);

    // ============================================================================
    // UNBOXED SYSTEM FUNCTIONS (fn_*_u)
    // Native C implementations that bypass Item boxing overhead.
    // Called directly when types are known at compile time.
    // ============================================================================

    // Math functions (double → double)
    double fn_pow_u(double base, double exponent);
    double fn_min2_u(double a, double b);
    double fn_max2_u(double a, double b);

    // Integer operations
    int64_t fn_abs_i(int64_t x);
    double fn_abs_f(double x);
    int64_t fn_neg_i(int64_t x);
    double fn_neg_f(double x);

    // Collection length — type-specialized native variants
    // G0: these return a Lambda `int`, so they return int's one native
    // representation. Their receivers are raw pointers, so none can fail.
    int64_t fn_len_l(List* list);       // list length
    int64_t fn_len_s(String* str);      // string length (UTF-8 aware)
    int64_t fn_len_e(Element* elmt);    // element children count

    // Boolean operations
    Bool fn_not_u(Bool x);

    // Sign function
    int64_t fn_sign_i(int64_t x);
    int64_t fn_sign_f(double x);

    // String.raw tagged template literal
    Item js_string_raw(Item* args, int argc);

    // Mark method functions (non-constructable, no prototype)
    void js_mark_method_func(Item fn_item);

    // Mark functions as strict mode
    void js_mark_strict_func(Item fn_item);

    // Rounding functions (int versions return identity)
    int64_t fn_floor_i(int64_t x);
    int64_t fn_ceil_i(int64_t x);
    int64_t fn_round_i(int64_t x);

    // vector manipulation functions
    Item fn_reverse(Item a);
    void array_reverse_in_place(Array* array);
    Item fn_sort1(Item a);
    Item fn_sort2(Item a, Item dir);
    Array* fn_order_rows(Item rows, Item keys, int64_t desc_mask, int64_t key_count);  // `order by` (S6.2)
    Item fn_unique(Item a);
    Item fn_take(Item a, Item n);
    Item fn_drop(Item a, Item n);
    Item fn_slice(Item a, Item start, Item end);
    Item fn_slice3(Item a, Item start, Item end);
    Item fn_slice2(Item a, Item start);
    Item fn_clip(Item a, Item lo, Item hi);
    Item fn_all(Item a);
    Item fn_any(Item a);
    Item fn_subview(Item arr, Item start, Item end);  // read-only view over arr[start..end]
    Item fn_is_view(Item arr);                     // returns true if arr is a view
    Item fn_reshape(Item arr, Item shape);         // returns view with new shape (contiguous required)
    Item fn_shape(Item arr);                       // returns shape as a list
    Item fn_ndim(Item arr);                        // returns number of dimensions (int)
    Item fn_transpose(Item arr);                   // view with reversed axes
    Item fn_flatten(Item arr);                     // owned 1-D contiguous copy
    Item fn_ravel(Item arr);                       // 1-D view if contiguous, else copy
    Item fn_matmul(Item a, Item b);                // matrix product
    Item fn_concat(Item a, Item b);                // join along axis 0
    Item fn_stack(Item a, Item b);                 // stack along new leading axis
    Item pn_push(Item arr, Item value);            // append value to a growable array in place
    Item pn_splice(Item arr, Item start, Item count); // remove count elements at start, in place
    Item pn_push_cow(Item owner, Item value);
    Item pn_splice_cow(Item owner, Item start, Item count);
    Item array_num_push_open(Item owner, Item value);  // push onto an uncontracted packed array

    // image stencil engine: slide a Kh×Kw window over the spatial dims of `in`
    // (2-D H×W, or 3-D H×W×C applied per-channel) and reduce at each position.
    // op: 0=DOT 1=MIN 2=MAX 3=MEDIAN 4=MEAN.  border: 0=CONSTANT 1=EDGE 2=REFLECT
    // 3=WRAP.  pad_h/pad_w: window-start offset from each output's input position
    // (negative → centred at Kh/2, Kw/2 for same-size filtering; 0 → top-left for
    // pooling).  Result is ELEM_FLOAT64.  Covers convolution/morphology/rank/pooling.
    Item array_num_stencil(Item in, Item kernel, int op, int border, double border_value,
                           int64_t stride_h, int64_t stride_w, int64_t pad_h, int64_t pad_w);
    Item fn_convolve(Item img, Item kernel);       // weighted-sum correlation (DOT)
    Item fn_blur(Item img, Item ksize);            // box (mean) blur
    Item fn_image_process(Item img, Item options); // fused, bounded RGBA8 processing
    Item fn_image_raster(Item samples, Item options);
    Item fn_image_region(Item raster, Item rect, Item channel);
    Item fn_image_render(Item source, Item options);
    Item fn_image_decode(Item bytes, Item options);
    Item fn_image_encode(Item source, Item composite, Item options);
    Item fn_image_write_region(Item raster, Item samples, Item options);
    Item fn_image_patch(Item source, Item attributes, Item children);
    Item fn_image_edit(Item raster, Item options);
    Item pn_image_storage(Item raster);
    Item fn_image_capabilities(void);
    Item fn_erode(Item img, Item ksize);           // morphological min
    Item fn_dilate(Item img, Item ksize);          // morphological max
    Item fn_median_filter(Item img, Item ksize);   // rank (median) filter
    Item fn_maxpool(Item img, Item ksize);         // strided max pooling
    Item fn_avgpool(Item img, Item ksize);         // strided mean pooling
    // image I/O bridge: load/save PNG-JPEG-GIF, ubyte[0,255] <-> float[0,1]
    Item fn_load(Item path);                       // decode to (H,W,4) ubyte RGBA
    Item fn_save(Item img, Item path);             // encode an image to PNG
    Item fn_as_float(Item img);                    // ubyte [0,255] -> float [0,1]
    Item fn_as_ubyte(Item img);                    // float [0,1] -> ubyte [0,255]
    // point / colour / geometric image ops
    Item fn_invert(Item img);                      // photographic negative
    Item fn_gamma(Item img, Item g);               // gamma correction
    Item fn_threshold(Item img, Item t);           // binarize at threshold t
    Item fn_grayscale(Item img);                   // RGB -> luma, (H,W,C)->(H,W)
    Item fn_flip(Item img, Item axis);             // mirror along axis 0 (vert) / 1 (horiz)
    Item fn_rot90(Item img, Item k);               // rotate CCW by 90*k degrees
    Item fn_crop(Item img, Item rows, Item cols);  // owned copy of an inclusive region
    // histogram / segmentation / resize / warp
    Item fn_histogram(Item img, Item bins);        // 1-D value counts
    Item fn_otsu(Item img);                        // optimal threshold value
    Item fn_label(Item mask);                      // 4-connected component labels
    Item fn_resize(Item img, Item h, Item w);      // bilinear resample
    Item fn_rotate(Item img, Item deg);            // bilinear rotation about centre
    Item fn_affine_warp(Item img, Item m);         // 2x3 affine coordinate gather
    int64_t array_num_iter_count(ArrayNum* arr);   // shape[0] for N-D, length for 1-D
    ArrayNum* array_num_new_ndim(ArrayNumElemType elem_type, int64_t total, int ndim, int64_t* dims);
    Item array_num_at_nd(ArrayNum* arr, int ndim, int64_t* indices);   // multi-dim scalar read
    Item array_num_set_nd(ArrayNum* arr, int ndim, int64_t* indices, Item value); // multi-dim write
    Item array_num_set_nd_admitted(ArrayNum* arr, int ndim, int64_t* indices,
        Item value); // exact write after a typed boundary
    Item fn_zip(Item a, Item b);
    Item fn_range3(Item start, Item end, Item step);
    Item fn_math_quantile(Item a, Item p);
    Item fn_math_quantile_skip_null(Item a, Item p, bool skip_null);
    Item fn_reduce(Item collection, Item func);

    Item fn_to(Item a, Item b);
    Item fn_range_bound_error(Item a, Item b);

    // pipe operations
    typedef Item (*PipeMapFn)(Item item, Item index);
    Item fn_pipe_map(Item collection, PipeMapFn transform);
    Item fn_pipe_where(Item collection, PipeMapFn predicate);
    // Finish a mapping pipe or `that` collection in its source's kind
    // (S10.1.2v2, S10.1.5v2).
    Item pipe_end(Array* result, Item source);
    Item fn_pipe_call(Item collection, Item func);

    String* fn_string(Item item);
    String *fn_strcat(String *left, String *right);
    String *str_repeat(String *str, int64_t times);
    String *fn_strcat3(String *part0, String *part1, String *part2);
    String *fn_strcat4(String *part0, String *part1, String *part2, String *part3);
    String *fn_strcat5(String *part0, String *part1, String *part2, String *part3,
        String *part4);
    String *fn_strcat6(String *part0, String *part1, String *part2, String *part3,
        String *part4, String *part5);
    enum { LAMBDA_STRING_CONCAT_MAX_PARTS = 64 };
    String *fn_strcat_many(int64_t owned, int64_t count, ...);
    String *fn_strcat_values(int64_t owned, int64_t count, ...);
    String *fn_string_freeze(String *str);
    Item fn_normalize(Item str, Item type);
    Item fn_normalize1(Item str);           // normalize with default NFC
    Item fn_substring(Item str, Item start, Item end);
    Bool fn_contains(Item str, Item substr);
    Item fn_join(Item a, Item b);
    Item fn_join_consume_open_array(Item left, Item right);
    Item fn_join_fresh_array_item(Item left, Item element);
    // string functions
    Bool fn_starts_with(Item str, Item prefix);
    Bool fn_ends_with(Item str, Item suffix);
    Item fn_index_of(Item str, Item sub);
    Item fn_last_index_of(Item str, Item sub);
    int64_t fn_index_of_raw(Item str, Item sub);       // C/JS -1 sentinel adapter
    int64_t fn_last_index_of_raw(Item str, Item sub);  // C/JS -1 sentinel adapter
    Item fn_trim(Item str);
    Item fn_trim_start(Item str);
    Item fn_trim_end(Item str);
    Item fn_lower(Item str);
    Item fn_upper(Item str);
    Item fn_url_resolve(Item base, Item relative);
    Item fn_url_encode(Item text);
    Item fn_hash(Item value, Item algorithm);
    Item fn_verify_signature(Item key, Item message, Item signature, Item scheme);
    Item pn_uuid(void);
    Item fn_split(Item str, Item sep);
    // A literal ASCII split whose result stores tagged Items for guest arrays.
    Item fn_split_literal_items(Item str, Item sep);
    Item fn_split3(Item str, Item sep, Item keep_delim);
    Item fn_array_split(Item arr, int64_t n, int64_t axis);  // split typed array into n parts along axis
    // axis-aware reductions (2-arg): collapse `axis` of a typed N-D array
    Item fn_sum_axis(Item arr, Item axis);
    Item fn_avg_axis(Item arr, Item axis);
    Item fn_prod_axis(Item arr, Item axis);
    Item fn_cumsum_axis(Item arr, Item axis);    // running sum along axis (no collapse)
    Item fn_cumprod_axis(Item arr, Item axis);   // running product along axis (no collapse)
    Item fn_min_axis(Item arr, Item axis);       // min along axis (via min(arr, axis) / min(arr, axis:N))
    Item fn_max_axis(Item arr, Item axis);       // max along axis
    Item fn_mask_index(Item arr, Item mask);     // arr[mask] — boolean mask selection
    Item fn_index_assign(Item arr, Item idx, Item val);  // arr[mask] = v — masked write (procedural in-place)
    Item vec_cmp(Item a, Item b, int op);        // vectorized a OP b → ELEM_BOOL mask (op = operator-OPERATOR_EQ)
    // overload wrappers (the sysfunc dispatcher resolves fn_<name><argcount>)
    Item fn_sum1(Item arr);            Item fn_sum2(Item arr, Item axis);
    Item fn_avg1(Item arr);            Item fn_avg2(Item arr, Item axis);
    Item fn_math_prod1(Item arr);      Item fn_math_prod2(Item arr, Item axis);
    Item fn_math_mean1(Item arr);      Item fn_math_mean2(Item arr, Item axis);
    Item fn_math_median1(Item arr);
    Item fn_math_median2(Item arr, Item option);
    Item fn_math_variance1(Item arr);
    Item fn_math_variance2(Item arr, Item option);
    Item fn_math_deviation1(Item arr);
    Item fn_math_deviation2(Item arr, Item option);
    Item fn_math_quantile2(Item arr, Item p);
    Item fn_math_quantile3(Item arr, Item p, Item option);
    Item fn_math_cumsum1(Item arr);    Item fn_math_cumsum2(Item arr, Item axis);
    Item fn_math_cumprod1(Item arr);   Item fn_math_cumprod2(Item arr, Item axis);
    Item fn_split2(Item str, Item sep);  // overloaded alias for fn_split
    Item fn_ord(Item str);              // ord(str) - Unicode code point or null
    int64_t fn_ord_str(String* str);    // native raw variant with C/JS -1 sentinel
    Item fn_ord_str_item(String* str);  // native Lambda-facing nullable result
    Item fn_chr(Item codepoint);        // chr(int) - 1-char string from Unicode code point
    Item fn_io_cell_width(Item codepoint);
    Item fn_io_unicode_category(Item codepoint);
    Item fn_join2(Item list, Item sep);
    Item fn_replace(Item str, Item old_str, Item new_str);
    Item fn_replace3(Item str, Item old_str, Item new_str);  // overloaded alias for fn_replace
    Item fn_replace4(Item str, Item old_str, Item new_str, Item options);
    Item fn_find2(Item source, Item pattern);
    Item fn_find3(Item source, Item pattern, Item options);
    Item fn_len_find2(Item source, Item pattern);
    Item fn_len_find3(Item source, Item pattern, Item options);

    Type* base_type(TypeId type_id);
    Type* const_type(int64_t type_index);
    TypePattern* const_pattern(int64_t pattern_index);  // retrieve compiled pattern by index
    TypePattern* const_pattern_with_tl(int64_t pattern_index, void* type_list_ptr);

    // returns the type of the item
    Type* fn_type(Item item);
    TypeId item_type_id(Item item);  // returns the TypeId of an item (for MIR use)

    // returns the name of an element, function, or type as a symbol
    Symbol* fn_name(Item item);

    Item fn_input1(Item url);
    Item fn_input2(Item url, Item options);
    // PTH32: the force step `#`. Turns a reference into what it refers to:
    // `input(p)` plus trailing navigation for a provider path (PTH53v2), the
    // system value for a `sys.*` path, null for an absent in-memory reference.
    Item fn_force(Item ref);
    // PTH40: address-of. The reference the operand carries as its identity, or
    // null. Performs no I/O and mints no identity.
    Item fn_address_of(Item value);
    // PTH45v2: reference equality, `&a != null and &a == &b`.
    Bool fn_ref_eq(Item a, Item b);
    Item fn_temp1(Item name);
    Item fn_temp2(Item name, Item content);
    // Tier 3 (PTH60v3): the CRUD statements. Each RECORDS an edit into the
    // write set and answers null; nothing a reader can see changes until
    // `commit` (PTH61). Outside `open` the record autocommits (PTH63v2).
    Item fn_put_member(Item base, Item key, Item value);
    Item fn_put_node(Item node, Item value);
    Item fn_put_before(Item value, Item node);
    Item fn_put_after(Item value, Item node);
    Item fn_put_into(Item value, Item container);
    Item fn_del_member(Item base, Item key);
    Item fn_del_node(Item node);
    Item fn_commit(void);
    Item fn_rollback(void);
    // `open target { … }` (PTH68v3): the block is a bounded transaction whose
    // exit is a commit-or-rollback point.
    Item fn_open_begin(Item target);
    Item fn_open_end(Bool unwinding);
    Item fn_parse1(Item str);
    Item fn_parse2(Item str, Item options);
    Item fn_parse3(Item str, Item grammar, Item rule);
    Item fn_parse_html_fragment1(Item str);
    Item fn_format1(Item item);
    Item fn_format2(Item item, Item options);
    Item fn_error(Item message);  // raise a user-defined error
    Item fn_error2(Item message, Item source);  // an error wrapping `source` (S7.4.4)
    // stamps an error() call site onto the error that call just constructed
    Item lambda_error_stamp_site(Item error, const char* file, int64_t line, int64_t column);
    Item lambda_raise_operand(Item value, const char* file, int64_t line, int64_t column);
    Symbol* fn_symbol1(Item item);  // convert to symbol
    Item fn_symbol2(Item name, Item url);  // create namespaced symbol

    // view/edit template apply
    Item fn_apply1(Item target);
    Item fn_apply2(Item target, Item options);


    // datetime constructors
    DateTime fn_datetime0();                       // datetime() - current datetime
    DateTime fn_datetime1(Item arg);               // datetime(str) - parse from string
    DateTime fn_date0();                           // date() - current date
    DateTime fn_date1(Item arg);                   // date(dt) - extract date portion
    DateTime fn_date3(Item y, Item m, Item d);     // date(y,m,d) - construct from components
    DateTime fn_time0();                           // time() - current time
    DateTime fn_time1(Item arg);                   // time(dt) - extract time portion
    DateTime fn_time3(Item h, Item m, Item s);     // time(h,m,s) - construct from components
    DateTime fn_justnow();                         // justnow() - current ms timestamp

    // variadic parameter access
    List* set_vargs(List* vargs);     // set current variadic args, returns previous
    void restore_vargs(List* prev);   // restore previous variadic args
    Item fn_varg0();                  // varg() - get all variadic args as list
    Item fn_varg1(Item index);        // varg(n) - get nth variadic arg

    // procedural functions
    DateTime pn_now();          // now() - current datetime in UTC
    DateTime pn_today();        // today() - current date in UTC
    Item pn_print(Item item);
    double pn_clock();        // clock() - high-resolution monotonic time in seconds
    Item pn_cmd1(Item cmd);
    Item pn_cmd2(Item cmd, Item args);
    Item pn_fetch(Item url, Item options);
    Item pn_output2(Item source, Item target);            // output(data, trg) - writes data to target, returns bytes written
    Item pn_output3(Item source, Item target, Item options);  // output(data, trg, options) - options: map {format, mode, atomic}, symbol/string (format), or null
    Item pn_output_append(Item source, Item target);      // used by |>> pipe operator (append mode)

    // io module functions (procedural)
    Item pn_io_copy(Item src, Item dst);
    Item pn_io_read(Item target);
    Item pn_io_exec2(Item program, Item args);
    Item pn_io_exec3(Item program, Item args, Item options);
    Item pn_io_move(Item src, Item dst);
    Item pn_io_delete(Item path);
    Item pn_io_mkdir(Item path);
    Item pn_io_create_file(Item path, Item content);
    Item pn_io_touch(Item path);
    Item pn_io_symlink(Item target, Item link);
    Item pn_io_chmod(Item path, Item mode);
    Item pn_io_rename(Item old_path, Item new_path);
    Item pn_io_grep2(Item source, Item pattern);
    Item pn_io_grep3(Item source, Item pattern, Item options);
    Item pn_io_text_search2(Item source, Item query);
    Item pn_io_text_search3(Item source, Item query, Item options);
    Item pn_io_fetch1(Item target);
    Item pn_io_fetch2(Item target, Item options);
    // lambda.io.mail (D9.3.4; vibe/Lambda_Demo_Mail.md §5.3)
    Item pn_io_mail_open(Item url, Item options);
    Item pn_io_mail_command2(Item session, Item command);
    Item pn_io_mail_command3(Item session, Item command, Item args);
    Item pn_io_mail_command4(Item session, Item command, Item args, Item options);
    Item pn_io_mail_idle(Item session, Item seconds);
    Item pn_io_mail_send(Item url, Item options, Item message);
    // lambda.io.secret (D9.3.4; vibe/Lambda_Demo_Mail.md ML6)
    Item pn_io_secret_get(Item service, Item account);
    Item pn_io_secret_set(Item service, Item account, Item value);
    Item pn_io_secret_delete(Item service, Item account);
    // lambda.io.loopback (D9.3.4; vibe/Lambda_Demo_Mail.md MG7)
    Item pn_io_websocket_open1(Item url);
    Item pn_io_websocket_open2(Item url, Item options);
    Item pn_io_websocket_send(Item connection, Item data);
    Item pn_io_websocket_receive1(Item connection);
    Item pn_io_websocket_receive2(Item connection, Item timeout_ms);
    Item pn_io_websocket_close(Item connection);
    Item pn_io_websocket_close2(Item connection, Item code);
    Item pn_io_websocket_close3(Item connection, Item code, Item reason);
    Item pn_io_websocket_listen(Item port);
    Item pn_io_websocket_listen2(Item port, Item options);
    Item pn_io_websocket_accept1(Item listener);
    Item pn_io_websocket_accept2(Item listener, Item timeout_ms);
    Item pn_io_sqlite_database(Item path);
    Item pn_io_sqlite_existing(Item path);
    Item pn_io_sqlite_create(Item path);
    Item pn_io_sqlite_clone(Item source, Item path);
    Item pn_io_sqlite_preimage(Item handle, Item path);
    Item pn_io_sqlite_snapshot_digest(Item handle);
    Item pn_io_sqlite_execute2(Item path, Item sql);
    Item pn_io_sqlite_execute3(Item path, Item sql, Item params);
    Item pn_io_sqlite_query2(Item path, Item sql);
    Item pn_io_sqlite_query3(Item path, Item sql, Item params);
    Item pn_io_rdb_database(Item uri, Item driver);
    Item pn_io_rdb_schema(Item handle);
    Item pn_io_rdb_details(Item handle);
    Item pn_io_rdb_execute2(Item handle, Item sql);
    Item pn_io_rdb_execute3(Item handle, Item sql, Item params);
    Item pn_io_rdb_fetch(Item handle, Item count);
    Item pn_io_rdb_finish(Item handle);
    Item pn_io_rdb_spool_begin(Item handle, Item path);
    Item pn_io_rdb_spool_read(Item handle, Item first, Item count);
    Item pn_io_rdb_spool_close(Item handle);
    Item pn_io_rdb_spool_seal(Item handle);
    Item pn_io_rdb_export_begin(Item handle, Item path);
    Item pn_io_rdb_export_write(Item handle, Item content);
    Item pn_io_rdb_export_commit(Item handle);
    Item pn_io_rdb_export_abort(Item handle);
    Item pn_io_loopback_listen(void);
    Item pn_io_loopback_accept(Item listener, Item seconds, Item page);

    // bitwise functions (integer operations)
    int64_t fn_band(int64_t a, int64_t b);
    int64_t fn_bor(int64_t a, int64_t b);
    int64_t fn_bxor(int64_t a, int64_t b);
    int64_t fn_bnot(int64_t a);
    int64_t fn_shl(int64_t a, int64_t b);
    int64_t fn_shr(int64_t a, int64_t b);
    Item fn_band_item(Item a, Item b);
    Item fn_bor_item(Item a, Item b);
    Item fn_bxor_item(Item a, Item b);
    Item fn_bnot_item(Item a);
    Item fn_shl_item(Item a, Item b);
    Item fn_shr_item(Item a, Item b);
    Item fn_ushr_item(Item a, Item b);

    // compound assignment support (procedural only)
    Item fn_mutable_value(Item value);
    Item fn_array_set(Array* arr, int64_t index, Item value);
    Item fn_map_set(Item map, Item key, Item value);
    // S16.8.9 runtime builder for a literal that contains a computed NameKey.
    Item map_literal_begin(void);
    Item map_literal_put(Item owner, Item key, Item value);
    Item map_literal_spread(Item owner, Item source);
    Element* elmt_literal_begin(struct TypeElmt* type);
    // one COW-kind policy for runtime capture and direct MIR (D5.4.1).
    static inline bool cow_type_is_container(TypeId type) {
        switch (type) {
        case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_MAP:
        case LMD_TYPE_ELEMENT: case LMD_TYPE_VMAP: case LMD_TYPE_VARRAY:
        case LMD_TYPE_VELMT: return true;
        default: return false;
        }
    }
    bool cow_item_is_container(Item value);
    bool cow_profile_enabled(void);
    Item cow_mark_shared(Item value);
    Item cow_mark_shared_profiled(Item value);
    Item array_num_set_cow_idx(Item owner, int64_t index, Item value);
    Item index_assign_cow(Item owner, Item key, Item value);
    Item cow_capture_value(Item value);
    Item cow_capture_value_profiled(Item value);
    Item cow_bind_rmw_handle(Item root, Item value, int64_t count, Item key1, Item key2);  // CW34
    Item cow_bind_rmw_root(Item root, Item value);
    // S9.3.1 insertion capture is unconditional; the helper remains the
    // runtime and direct MIR share the container-kind policy above.
    // Capture every field of a freshly built shaped literal (S9.3.1).
    void cow_mark_shape_children(struct TypeMap* type, void* data);
    Item cow_bind_var(Item value);
    Item cow_bind_var_profiled(Item value);
    Item cow_prepare_write(Item old);
    Item cow_prepare_write_profiled(Item old);

    // LR07-7/LR08-3 root-honesty witness.  Emitted by MIR Direct only when
    // LAMBDA_ROOT_WITNESS is set, at every point the transpiler DECLINES to
    // allocate a GC root slot: it asserts the unrooted word does not reference
    // GC-owned memory, so the collector's trust in static types is tested
    // rather than assumed.
    bool lambda_root_witness_enabled(void);
    // Level 2 additionally probes expression temporaries: every register live
    // across a may-GC call that the root machinery never made a candidate.
    int  lambda_root_witness_level(void);
    bool lambda_root_witness_temporaries(void);
    void lambda_jit_root_witness(uint64_t raw, int64_t claimed_type_id,
        const char* site, const char* binding, const char* func);
    void lambda_root_witness_dump(void);
    // Optional release-safe COW instrumentation.  It stays dormant unless
    // COW_EXEC_PROFILE is enabled, and raw JS/host setters never call it.
    void cow_profile_note_vmap_snapshot(void);
    void cow_profile_note_vmap_rejection(void);
    void cow_profile_dump(void);
    // MIR Direct's opt-in executed call census. IDs are registered while
    // lowering and incremented only by diagnostic MIR (D6.4.4).
    enum LambdaExecCallKind {
        LAMBDA_EXEC_CALL_HELPER = 1,
        LAMBDA_EXEC_CALL_RAW = 2,
        LAMBDA_EXEC_CALL_BOXED = 3,
        LAMBDA_EXEC_CALL_INDIRECT = 4,
        LAMBDA_EXEC_CALL_UNKNOWN = 5,
        LAMBDA_EXEC_ROOT_STORE = 6,
        LAMBDA_EXEC_ROOT_RELOAD = 7,
        LAMBDA_EXEC_FRAME_ENTRY = 8,
        LAMBDA_EXEC_DENSE_ATTEMPT = 9,
        LAMBDA_EXEC_DENSE_FAST = 10,
        LAMBDA_EXEC_FIELD_ATTEMPT = 11,
        LAMBDA_EXEC_FIELD_FAST = 12,
        LAMBDA_EXEC_LAYOUT_ITEMS_RELOAD = 13,
        LAMBDA_EXEC_LAYOUT_LENGTH_RELOAD = 14,
    };
    bool lambda_exec_profile_enabled(void);
    uint64_t lambda_exec_profile_register_call(const char* name,
        uint64_t kind);
    uint64_t lambda_exec_profile_note_call(uint64_t id);
    // LambdaJS realm-slot reservation census row (exec profile)
    void cow_profile_count_js_realm_reservation(void);
    Item array_set_cow(Item owner, Item key, Item value);
    Item member_set_cow(Item owner, Item key, Item value);
    Item member_set_cow_profiled(Item owner, Item key, Item value);
    Item map_set_cow(Item owner, Item key, Item value);
    Item map_set_cow_profiled(Item owner, Item key, Item value);
    Item map_set_cow_resolved(Item owner, NameId key, Item value,
        const struct TypeMap* shape, struct ShapeEntry* field);
    Item map_set_cow_resolved_profiled(Item owner, NameId key, Item value,
        const struct TypeMap* shape, struct ShapeEntry* field);
    Item lambda_array_scalar_snapshot(Item value, void* scratch, int64_t scratch_bytes);
    bool cow_profile_sites_enabled(void);
    Item cow_path_set_raw(Item owner, Item key, Item value);
    Item cow_path_index(Item owner, Item key);
    Item cow_path_set_keys(Item owner, Item value, const Item* keys, int count, bool inplace);
    Item cow_path_set(Item owner, Item path, Item value);
    // NM-O8: nested store that leaves the ROOT alone (var / plain-pn params).
    Item cow_path_set_inplace(Item owner, Item path, Item value);
    // CW25: detach root..leaf and return the leaf, for a `var` path borrow.
    Item cow_path_borrow(Item owner, Item path);
    Item lambda_var_home_check_kind(Item before, Item after, const char* procedure);
    bool cow_item_is_unique(Item value);
    Item cow_path_borrow_home(Item owner, Item path);
    // CW25: bounded descriptor form for compiler-known member/int paths.
    Item cow_path_borrow_fixed(Item owner, int64_t count,
        Item key0, Item key1, Item key2);
    // Tune31 Phase III A1: shape-checked direct field borrow with the fixed
    // descriptor retained as the mismatch path.
    Item cow_path_borrow_typed_map_field(Item owner, struct TypeMap* expected_shape,
        struct ShapeEntry* expected_field, Item fallback_key);
    // builtin mutator place (push/splice/set): spine detach; a non-container
    // link is returned as the value for the mutator to reject
    Item cow_place_leaf(Item owner, Item path);
    Item cow_place_leaf_keys(Item owner, const Item* keys, int64_t count);
    // key-span borrow: `count` (1..LAMBDA_PATH_KEYS_MAX) evaluated keys
    // in caller memory, rooted by the walker
    Item cow_path_borrow_keys(Item owner, const Item* keys, int64_t count);

    // runtime type coercion for typed array annotations (int[], float[], etc.)
    // converts generic Array/List to typed array, or validates existing typed array
    // returns pointer to the coerced typed array, or NULL if elements are incompatible
    void* ensure_typed_array(Item item, TypeId element_type_id);
    void* ensure_sized_array(Item item, int64_t elem_type);

    // VMap system functions
    Item vmap_new();
    Item vmap_from_array(Item array_item);
    bool virtual_host_member_set(Item owner, Item key, Item value, Item* out);
    Item vmap_set(Item vmap_item, Item key, Item value);
    Item vmap_set_cow(Item owner, Item key, Item value);
    Item vmap_clone_for_cow(Item source);

    // reactive UI: emit event to parent template handler
    Item pn_emit(Item event_name, Item event_data);
    // called from Radiant side — dispatches emitted event up the DOM ancestry
    Item dispatch_emit(Item event_name, Item event_data);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus
#include "lambda/core/value.hpp"
#include "runtime/lambda-root-frame.hpp"
#endif
