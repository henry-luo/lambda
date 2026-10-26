// IME14/IME15: InputMethodKit is a host for the Lambda reducer, not a second engine.
#import <AppKit/AppKit.h>
#import <InputMethodKit/InputMethodKit.h>
#import <Carbon/Carbon.h>

#include "../../runtime/transpiler.hpp"
#include "../../runtime/interp.hpp"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../input/input.hpp"
#include "../../../lib/host_paths.h"
#include "../../../lib/log.h"
#include "../../../lib/memtrack.h"
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static const char* kPackage = "lambda.ime.chinese";
static const char* kDictionaryPackage = "lambda.ime.chinese.dictionary";
static const char* kBundleId = "org.lambda-script.ChineseIME";
static const char* kProfiles[] = {"zh-Hans", "zh-Hant"};
static Runtime g_runtime;
static uint64_t g_engine_roots[2] = {};
static uint64_t g_base_roots[2] = {};
static uint64_t g_edit_roots[2] = {};
static bool g_engines_registered = false;
static uint64_t g_engine_generation[2] = {};
static IMKServer* g_server = nil;
static IMKCandidates* g_panel = nil;
static NSString* g_support_dir = nil;
static NSString* g_settings_file = nil;
static int g_selected_profile = 0;
static bool g_fuzzy_on = true;
static NSArray* g_requested_fuzzy_pairs = nil;
static NSArray* g_supported_fuzzy_pairs = nil;
static NSMutableSet* g_enabled_fuzzy_pairs = nil;

static Item rooted_word(uint64_t word) { return {.item = word}; }
static Item ime_string(const char* value) {
    return {.item = s2it(heap_strcpy(value, strlen(value)))};
}
static NSString* native_string(Item value) {
    String* source = get_type_id(value) == LMD_TYPE_STRING ? value.get_string() : nullptr;
    return source ? [[[NSString alloc] initWithBytes:source->chars length:source->len
        encoding:NSUTF8StringEncoding] autorelease] : @"";
}
static void ime_set(Item object, const char* key, Item value) {
    RootFrame roots(2);
    Rooted<Item> name(roots, ime_string(key));
    Rooted<Item> held(roots, value);
    vmap_set(object, name.get(), held.get());
}
static Item module_call(const char* package, const char* export_name, const Item* args, int count) {
    Item value = interp_call_package_export(&g_runtime, package, export_name, args, count);
    if (item_is_error(value)) log_error("macos-ime: package call %s.%s failed", package, export_name);
    return value;
}
static Item ime_call(const char* export_name, const Item* args, int count) {
    return module_call(kPackage, export_name, args, count);
}
static Item dictionary_call(const char* export_name, const Item* args, int count) {
    return module_call(kDictionaryPackage, export_name, args, count);
}
static NSString* user_file(int profile) {
    return [g_support_dir stringByAppendingPathComponent:
        [NSString stringWithFormat:@"%s-user.json", kProfiles[profile]]];
}
static NSString* edits_file(int profile) {
    return [g_support_dir stringByAppendingPathComponent:
        [NSString stringWithFormat:@"%s-dictionary.json", kProfiles[profile]]];
}
static void save_settings(void) {
    NSDictionary* settings = @{ @"profile":[NSString stringWithUTF8String:kProfiles[g_selected_profile]],
        @"fuzzy":@(g_fuzzy_on), @"fuzzyPairs":[g_enabled_fuzzy_pairs allObjects] };
    if (![settings writeToFile:g_settings_file atomically:YES])
        log_error("macos-ime: could not save input-source settings");
}
static bool engine_for_profile(int profile) {
    if (profile < 0 || profile > 1) return false;
    if (g_engine_roots[profile]) return true;
    RootFrame roots(3);
    Rooted<Item> name(roots, ime_string(kProfiles[profile]));
    Item argument = name.get();
    Item engine = ime_call("load", &argument, 1);
    if (item_is_error(engine) || get_type_id(engine) != LMD_TYPE_MAP) return false;
    g_base_roots[profile] = engine.item;
    NSString* path_name = edits_file(profile);
    Rooted<Item> edits(roots, ItemNull);
    if ([[NSFileManager defaultManager] fileExistsAtPath:path_name]) {
        Rooted<Item> path(roots, ime_string([path_name UTF8String]));
        Item args[2] = {path.get(), rooted_word(g_base_roots[profile])};
        edits.set(dictionary_call("load_edits", args, 2));
    } else {
        Item argument = name.get();
        edits.set(dictionary_call("empty", &argument, 1));
    }
    if (item_is_error(edits.get()) || get_type_id(edits.get()) != LMD_TYPE_MAP) return false;
    g_edit_roots[profile] = edits.get().item;
    Item restore_args[2] = {rooted_word(g_base_roots[profile]), rooted_word(g_edit_roots[profile])};
    Item active = dictionary_call("restore", restore_args, 2);
    if (item_is_error(active) || get_type_id(active) != LMD_TYPE_MAP) return false;
    g_engine_roots[profile] = active.item;
    return true;
}
static bool change_dictionary(int profile, NSString* word, NSString* reading, BOOL present) {
    if (!engine_for_profile(profile)) return false;
    RootFrame roots(5);
    Rooted<Item> word_arg(roots, ime_string([word UTF8String]));
    Rooted<Item> reading_arg(roots, ime_string([reading UTF8String]));
    Item change_args[5] = {rooted_word(g_edit_roots[profile]), rooted_word(g_base_roots[profile]),
        word_arg.get(), reading_arg.get(), {.item = b2it(present)}};
    Rooted<Item> changed(roots, dictionary_call("change", change_args, 5));
    if (item_is_error(changed.get()) || get_type_id(changed.get()) != LMD_TYPE_MAP) return false;
    Item restore_args[2] = {rooted_word(g_base_roots[profile]), changed.get()};
    Rooted<Item> active(roots, dictionary_call("restore", restore_args, 2));
    if (item_is_error(active.get()) || get_type_id(active.get()) != LMD_TYPE_MAP) return false;
    Rooted<Item> path(roots, ime_string([edits_file(profile) UTF8String]));
    Item save_args[3] = {changed.get(), path.get(), rooted_word(g_base_roots[profile])};
    Item saved = dictionary_call("save_edits", save_args, 3);
    if (item_is_error(saved)) return false;
    g_edit_roots[profile] = changed.get().item;
    g_engine_roots[profile] = active.get().item;
    g_engine_generation[profile]++;
    return true;
}
static bool prepare_paths(void) {
    NSURL* resource = [[NSBundle mainBundle] resourceURL];
    if (!resource) return false;
    NSString* home = [[resource URLByAppendingPathComponent:@"lmd"] path];
    NSString* support = [NSHomeDirectory() stringByAppendingPathComponent:
        [@"Library/Application Support/" stringByAppendingString:
            [NSString stringWithUTF8String:kBundleId]]];
    NSString* cache = [support stringByAppendingPathComponent:@"Cache"];
    NSString* scratch = [support stringByAppendingPathComponent:@"Temp"];
    NSString* logs = [support stringByAppendingPathComponent:@"Logs"];
    NSFileManager* fm = [NSFileManager defaultManager];
    for (NSString* path in @[support, cache, scratch, logs]) {
        if (![fm createDirectoryAtPath:path withIntermediateDirectories:YES
                attributes:nil error:nil]) return false;
    }
    g_support_dir = [support copy];
    g_settings_file = [[support stringByAppendingPathComponent:@"settings.plist"] copy];
    NSDictionary* saved = [NSDictionary dictionaryWithContentsOfFile:g_settings_file];
    if ([saved[@"profile"] isEqualToString:@"zh-Hant"]) g_selected_profile = 1;
    if ([saved[@"fuzzy"] isKindOfClass:[NSNumber class]])
        g_fuzzy_on = [saved[@"fuzzy"] boolValue];
    if ([saved[@"fuzzyPairs"] isKindOfClass:[NSArray class]])
        g_requested_fuzzy_pairs = [saved[@"fuzzyPairs"] copy];
    host_paths_init_from_env();
    return host_paths_set(HOST_PATH_HOME, [home fileSystemRepresentation]) &&
        host_paths_set(HOST_PATH_CACHE, [cache fileSystemRepresentation]) &&
        host_paths_set(HOST_PATH_TEMP, [scratch fileSystemRepresentation]) &&
        host_paths_set(HOST_PATH_LOG, [logs fileSystemRepresentation]);
}
static bool prepare_runtime(void) {
    if (!prepare_paths()) return false;
    log_set_output_dir(host_path(HOST_PATH_LOG));
    log_init("");
    memtrack_init(MEMTRACK_MODE_OFF);
    runtime_init(&g_runtime);
    // The retained evaluator owns every controller's rooted session (D5.3.3/D5.4.1).
    Input* bootstrap = run_script_mir(&g_runtime, "0", (char*)"<macos-ime>", false);
    if (!bootstrap || item_is_error(bootstrap->root)) return false;
    g_engines_registered = heap_try_register_gc_root_range(g_engine_roots, 2) &&
        heap_try_register_gc_root_range(g_base_roots, 2) &&
        heap_try_register_gc_root_range(g_edit_roots, 2);
    if (!g_engines_registered || !engine_for_profile(g_selected_profile)) return false;
    RootFrame roots(1);
    Rooted<Item> options(roots, ime_call("fuzzy_options", nullptr, 0));
    if (item_is_error(options.get())) return false;
    NSMutableArray* names = [NSMutableArray array];
    for (int64_t at = 0, count = fn_len(options.get()); at < count; at++) {
        NSString* name = native_string(item_attr(item_at(options.get(), at), "name"));
        if ([name length]) [names addObject:name];
    }
    g_supported_fuzzy_pairs = [names copy];
    g_enabled_fuzzy_pairs = [[NSMutableSet alloc] initWithArray:
        g_requested_fuzzy_pairs ?: g_supported_fuzzy_pairs];
    [g_enabled_fuzzy_pairs intersectSet:[NSSet setWithArray:g_supported_fuzzy_pairs]];
    return [g_supported_fuzzy_pairs count] > 0;
}
static Item fuzzy_setting(RootFrame& roots) {
    if (!g_fuzzy_on) return ime_string("none");
    Rooted<Item> pairs(roots, {.array = array_plain()});
    for (NSString* name in g_supported_fuzzy_pairs) {
        if ([g_enabled_fuzzy_pairs containsObject:name])
            array_push(pairs.get().array, ime_string([name UTF8String]));
    }
    return pairs.get();
}
static NSButton* dictionary_button(NSString* title, SEL action, id target, NSRect frame) {
    NSButton* button = [[[NSButton alloc] initWithFrame:frame] autorelease];
    [button setTitle:title];
    [button setBezelStyle:NSBezelStyleRounded];
    [button setTarget:target];
    [button setAction:action];
    return button;
}

@interface LambdaDictionaryPanel : NSObject <NSTableViewDataSource, NSTableViewDelegate> {
    NSPanel* _panel;
    NSSearchField* _search;
    NSTextField* _word;
    NSTextField* _reading;
    NSTableView* _table;
    NSArray* _rows;
    uint64_t _cursor_root;
    BOOL _cursor_registered;
    int _profile;
}
- (void)show;
@end
static LambdaDictionaryPanel* g_dictionary_panel = nil;

@implementation LambdaDictionaryPanel
- (id)init {
    self = [super init];
    if (!self) return nil;
    _cursor_registered = heap_try_register_gc_root(&_cursor_root);
    if (!_cursor_registered) return nil;
    _panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 680, 470)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    [_panel setReleasedWhenClosed:NO];
    [_panel setTitle:@"Lambda Chinese Dictionary"];
    [_panel center];
    NSView* content = [_panel contentView];
    _search = [[NSSearchField alloc] initWithFrame:NSMakeRect(20, 420, 520, 28)];
    [_search setPlaceholderString:@"Search word or pinyin"];
    [_search setTarget:self];
    [_search setAction:@selector(search:)];
    [content addSubview:_search];
    [content addSubview:dictionary_button(@"Search", @selector(search:), self,
        NSMakeRect(550, 420, 110, 28))];
    NSScrollView* scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(20, 105, 640, 305)] autorelease];
    [scroll setHasVerticalScroller:YES];
    _table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 640, 305)];
    for (NSString* name in @[@"Word", @"Pinyin", @"Weight"]) {
        NSTableColumn* column = [[[NSTableColumn alloc] initWithIdentifier:name] autorelease];
        [column setTitle:name];
        [column setWidth:[name isEqualToString:@"Weight"] ? 110 : 240];
        [_table addTableColumn:column];
    }
    [_table setDelegate:self];
    [_table setDataSource:self];
    [scroll setDocumentView:_table];
    [content addSubview:scroll];
    [content addSubview:dictionary_button(@"Next page", @selector(nextPage:), self,
        NSMakeRect(540, 70, 120, 28))];
    _word = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 28, 190, 28)];
    [_word setPlaceholderString:@"Word"];
    [content addSubview:_word];
    _reading = [[NSTextField alloc] initWithFrame:NSMakeRect(220, 28, 190, 28)];
    [_reading setPlaceholderString:@"Pinyin, separated by spaces"];
    [content addSubview:_reading];
    [content addSubview:dictionary_button(@"Add", @selector(addWord:), self,
        NSMakeRect(430, 28, 100, 28))];
    [content addSubview:dictionary_button(@"Delete selected", @selector(deleteWord:), self,
        NSMakeRect(540, 28, 120, 28))];
    return self;
}
- (void)dealloc {
    if (_cursor_registered) heap_unregister_gc_root(&_cursor_root);
    [_rows release];
    [_search release];
    [_word release];
    [_reading release];
    [_table release];
    [_panel release];
    [super dealloc];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView { return [_rows count]; }
- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
    return _rows[row][[column identifier]];
}
- (void)tableViewSelectionDidChange:(NSNotification*)notice {
    NSInteger row = [_table selectedRow];
    if (row < 0 || row >= (NSInteger)[_rows count]) return;
    [_word setStringValue:_rows[row][@"Word"]];
    [_reading setStringValue:_rows[row][@"Pinyin"]];
}
- (void)refresh {
    RootFrame roots(3);
    Rooted<Item> query(roots, ime_string([[_search stringValue] UTF8String]));
    Item args[4] = {rooted_word(g_engine_roots[_profile]), query.get(),
        _cursor_root ? rooted_word(_cursor_root) : ItemNull, {.item = i2it(30)}};
    Rooted<Item> page(roots, dictionary_call("browse", args, 4));
    if (item_is_error(page.get())) { NSBeep(); return; }
    Item entries = item_attr(page.get(), "items");
    NSMutableArray* rows = [NSMutableArray array];
    for (int64_t at = 0, count = fn_len(entries); at < count; at++) {
        Item entry = item_at(entries, at);
        [rows addObject:@{@"Word":native_string(item_attr(entry, "word")),
            @"Pinyin":native_string(item_attr(entry, "reading")),
            @"Weight":@(item_attr(entry, "weight").int_val)}];
    }
    [_rows release];
    _rows = [rows copy];
    Item next = item_attr(page.get(), "next");
    _cursor_root = get_type_id(next) == LMD_TYPE_MAP ? next.item : 0;
    [_table reloadData];
}
- (void)show {
    _profile = g_selected_profile;
    _cursor_root = 0;
    [_search setStringValue:@""];
    [self refresh];
    [_panel makeKeyAndOrderFront:nil];
}
- (void)search:(id)sender { _cursor_root = 0; [self refresh]; }
- (void)nextPage:(id)sender { if (_cursor_root) [self refresh]; }
- (void)addWord:(id)sender {
    if (!change_dictionary(_profile, [_word stringValue], [_reading stringValue], YES)) {
        NSBeep(); return;
    }
    _cursor_root = 0;
    [self refresh];
}
- (void)deleteWord:(id)sender {
    NSInteger row = [_table selectedRow];
    if (row < 0 || row >= (NSInteger)[_rows count]) { NSBeep(); return; }
    NSDictionary* selected = _rows[row];
    if (!change_dictionary(_profile, selected[@"Word"], selected[@"Pinyin"], NO)) {
        NSBeep(); return;
    }
    _cursor_root = 0;
    [self refresh];
}
@end
static void replace_marked(NSString** slot, NSString* value) {
    NSString* next = [value copy];
    [*slot release];
    *slot = next;
}
static void replace_shown(NSArray** slot, NSArray* value) {
    NSArray* next = [value copy];
    [*slot release];
    *slot = next;
}

@interface LambdaChineseController : IMKInputController {
    uint64_t _session_root;
    BOOL _session_registered;
    NSString* _marked;
    NSArray* _shown;
    NSInteger _page;
    BOOL _expanded;
    int _profile;
    BOOL _fuzzy;
    uint64_t _generation;
}
- (BOOL)resetProfile:(int)profile;
- (void)finishCompositionForClient:(id)client;
@end

@implementation LambdaChineseController
- (id)initWithServer:(IMKServer*)server delegate:(id)delegate client:(id)client {
    self = [super initWithServer:server delegate:delegate client:client];
    if (!self) return nil;
    _session_registered = heap_try_register_gc_root(&_session_root);
    if (!_session_registered || ![self resetProfile:g_selected_profile]) {
        if (_session_registered) heap_unregister_gc_root(&_session_root);
        return nil;
    }
    return self;
}
- (BOOL)resetProfile:(int)profile {
    if (!engine_for_profile(profile)) return NO;
    RootFrame roots(4);
    Rooted<Item> settings(roots, vmap_new());
    Rooted<Item> fuzzy(roots, fuzzy_setting(roots));
    ime_set(settings.get(), "fuzzy", fuzzy.get());
    ime_set(settings.get(), "page_size", {.item = i2it(7)});
    Item args[2] = {rooted_word(g_engine_roots[profile]), settings.get()};
    Item session = ime_call("session", args, 2);
    if (item_is_error(session) || get_type_id(session) != LMD_TYPE_MAP) return NO;
    _session_root = session.item;
    _profile = profile;
    _fuzzy = g_fuzzy_on;
    _generation = g_engine_generation[profile];
    replace_marked(&_marked, @"");
    replace_shown(&_shown, @[]);
    _page = 0;
    _expanded = NO;
    [g_panel hide];
    NSString* path_name = user_file(profile);
    if ([[NSFileManager defaultManager] fileExistsAtPath:path_name]) {
        Rooted<Item> path(roots, ime_string([path_name UTF8String]));
        Item path_arg = path.get();
        Item saved = ime_call("load_user", &path_arg, 1);
        if (!item_is_error(saved)) {
            Rooted<Item> user(roots, saved);
            Item with_args[2] = {rooted_word(_session_root), user.get()};
            Item updated = ime_call("with_user", with_args, 2);
            if (!item_is_error(updated)) _session_root = updated.item;
        }
    }
    return YES;
}
- (void)dealloc {
    if (_session_registered) heap_unregister_gc_root(&_session_root);
    [_marked release];
    [_shown release];
    [super dealloc];
}
- (NSUInteger)recognizedEvents:(id)sender {
    return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}
- (id)composedString:(id)sender { return _marked; }
- (NSAttributedString*)originalString:(id)sender {
    return [[[NSAttributedString alloc] initWithString:@""] autorelease];
}
- (void)hidePalettes { [g_panel hide]; }
- (void)activateServer:(id)sender {
    [super activateServer:sender];
    if (_profile != g_selected_profile || _fuzzy != g_fuzzy_on ||
            _generation != g_engine_generation[g_selected_profile])
        [self resetProfile:g_selected_profile];
}
- (void)finishCompositionForClient:(id)client {
    if (![_marked length] || !client) return;
    RootFrame roots(1);
    Rooted<Item> event(roots, vmap_new());
    ime_set(event.get(), "type", ime_string("keydown"));
    if (_expanded) {
        ime_set(event.get(), "key", ime_string("Escape"));
        [self dispatch:event.get() client:client];
    }
    ime_set(event.get(), "key", ime_string("Enter"));
    [self dispatch:event.get() client:client];
}
- (void)commitComposition:(id)sender {
    [self finishCompositionForClient:sender ?: [self client]];
}
- (void)deactivateServer:(id)sender {
    [self finishCompositionForClient:[self client]];
    [g_panel hide];
    [super deactivateServer:sender];
}
- (void)inputControllerWillClose {
    [self finishCompositionForClient:[self client]];
    [g_panel hide];
    [super inputControllerWillClose];
}
- (NSMenu*)menu {
    NSMenu* menu = [[[NSMenu alloc] initWithTitle:@"Lambda Chinese"] autorelease];
    NSMenuItem* simplified = [menu addItemWithTitle:@"简体 Chinese (Simplified)"
        action:@selector(selectSimplified:) keyEquivalent:@""];
    [simplified setTarget:self];
    [simplified setState:g_selected_profile == 0 ? NSControlStateValueOn : NSControlStateValueOff];
    NSMenuItem* traditional = [menu addItemWithTitle:@"繁體 Chinese (Traditional)"
        action:@selector(selectTraditional:) keyEquivalent:@""];
    [traditional setTarget:self];
    [traditional setState:g_selected_profile == 1 ? NSControlStateValueOn : NSControlStateValueOff];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* fuzzy = [menu addItemWithTitle:@"Fuzzy pinyin"
        action:@selector(toggleFuzzy:) keyEquivalent:@""];
    [fuzzy setTarget:self];
    [fuzzy setState:g_fuzzy_on ? NSControlStateValueOn : NSControlStateValueOff];
    NSMenu* pairs = [[[NSMenu alloc] initWithTitle:@"Fuzzy pinyin pairs"] autorelease];
    for (NSString* name in g_supported_fuzzy_pairs) {
        NSMenuItem* item = [pairs addItemWithTitle:[name stringByReplacingOccurrencesOfString:@"=" withString:@" ↔ "]
            action:@selector(toggleFuzzyPair:) keyEquivalent:@""];
        [item setTarget:self];
        [item setRepresentedObject:name];
        [item setState:[g_enabled_fuzzy_pairs containsObject:name] ? NSControlStateValueOn : NSControlStateValueOff];
    }
    NSMenuItem* pair_menu = [menu addItemWithTitle:@"Fuzzy pinyin pairs" action:nil keyEquivalent:@""];
    [menu setSubmenu:pairs forItem:pair_menu];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* dictionary = [menu addItemWithTitle:@"Edit dictionary…"
        action:@selector(showDictionary:) keyEquivalent:@""];
    [dictionary setTarget:self];
    return menu;
}
- (void)showDictionary:(id)sender {
    if (!g_dictionary_panel) g_dictionary_panel = [[LambdaDictionaryPanel alloc] init];
    [g_dictionary_panel show];
}
- (void)selectSimplified:(id)sender {
    [self finishCompositionForClient:[self client]];
    g_selected_profile = 0;
    save_settings();
    [self resetProfile:g_selected_profile];
}
- (void)selectTraditional:(id)sender {
    [self finishCompositionForClient:[self client]];
    g_selected_profile = 1;
    save_settings();
    [self resetProfile:g_selected_profile];
}
- (void)toggleFuzzy:(id)sender {
    [self finishCompositionForClient:[self client]];
    g_fuzzy_on = !g_fuzzy_on;
    save_settings();
    [self resetProfile:g_selected_profile];
}
- (void)toggleFuzzyPair:(NSMenuItem*)sender {
    [self finishCompositionForClient:[self client]];
    NSString* name = [sender representedObject];
    if ([g_enabled_fuzzy_pairs containsObject:name]) [g_enabled_fuzzy_pairs removeObject:name];
    else [g_enabled_fuzzy_pairs addObject:name];
    save_settings();
    [self resetProfile:g_selected_profile];
}
- (NSArray*)candidates:(id)sender { return _shown; }
- (void)candidateSelected:(NSAttributedString*)candidate {
    NSUInteger index = [_shown indexOfObjectPassingTest:^BOOL(id value, NSUInteger at, BOOL* stop) {
        return [[value string] isEqualToString:[candidate string]];
    }];
    if (index == NSNotFound) return;
    RootFrame roots(1);
    Rooted<Item> event(roots, vmap_new());
    ime_set(event.get(), "type", ime_string("keydown"));
    ime_set(event.get(), "key", ime_string("Candidate"));
    ime_set(event.get(), "candidate_index", {.item = i2it((int64_t)index)});
    [self dispatch:event.get() client:[self client]];
}
- (BOOL)dispatch:(Item)event client:(id)client {
    RootFrame roots(3);
    Rooted<Item> held_event(roots, event);
    Item args[3] = {rooted_word(g_engine_roots[_profile]), rooted_word(_session_root), held_event.get()};
    Rooted<Item> result(roots, ime_call("key", args, 3));
    if (item_is_error(result.get())) return NO;
    Item next = item_attr(result.get(), "session");
    if (get_type_id(next) != LMD_TYPE_MAP) return NO;
    _session_root = next.item;
    Item actions = item_attr(result.get(), "actions");
    BOOL handled = NO;
    for (int64_t index = 0, count = fn_len(actions); index < count; index++) {
        Item action = item_at(actions, index);
        NSString* kind = native_string(item_attr(action, "type"));
        if ([kind isEqualToString:@"compose"]) {
            replace_marked(&_marked, native_string(item_attr(action, "text")));
            int64_t caret = item_attr(action, "caret").int_val;
            NSUInteger offset = 0;
            for (NSUInteger cp = 0; cp < (NSUInteger)caret && offset < [_marked length]; cp++) {
                unichar first = [_marked characterAtIndex:offset++];
                if (CFStringIsSurrogateHighCharacter(first) && offset < [_marked length] &&
                        CFStringIsSurrogateLowCharacter([_marked characterAtIndex:offset])) offset++;
            }
            [client setMarkedText:_marked selectedRange:NSMakeRange(offset, 0)
                replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            handled = YES;
        } else if ([kind isEqualToString:@"commit"]) {
            [client insertText:native_string(item_attr(action, "text"))
                replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            replace_marked(&_marked, @"");
            Item user = item_attr(next, "user");
            Rooted<Item> path(roots, ime_string([user_file(_profile) UTF8String]));
            Item save_args[2] = {user, path.get()};
            ime_call("save_user", save_args, 2);
            handled = YES;
        } else if ([kind isEqualToString:@"cancel"]) {
            // IMKInputController's cancelComposition restores raw text; the reducer discards it.
            [client insertText:@"" replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
            replace_marked(&_marked, @"");
            handled = YES;
        } else if ([kind isEqualToString:@"candidates"]) {
            Item items = item_attr(action, "items");
            NSMutableArray* shown = [NSMutableArray array];
            for (int64_t at = 0, count = fn_len(items); at < count; at++) {
                NSString* value = native_string(item_attr(item_at(items, at), "text"));
                [shown addObject:[[[NSAttributedString alloc] initWithString:value] autorelease]];
            }
            replace_shown(&_shown, shown);
            _page = item_attr(action, "page").int_val;
            _expanded = item_attr(action, "expanded").bool_val != 0;
            [g_panel setPanelType:_expanded ? kIMKScrollingGridCandidatePanel :
                kIMKSingleRowSteppingCandidatePanel];
            if ([shown count]) {
                [g_panel updateCandidates];
                [g_panel show:kIMKLocateCandidatesBelowHint];
                handled = YES;
            } else [g_panel hide];
        }
    }
    return handled;
}
- (BOOL)handleEvent:(NSEvent*)event client:(id)client {
    if ([event type] != NSEventTypeKeyDown && [event type] != NSEventTypeFlagsChanged) return NO;
    NSString* key = [event charactersIgnoringModifiers];
    switch ([event keyCode]) {
        case kVK_LeftArrow: key = @"ArrowLeft"; break;
        case kVK_RightArrow: key = @"ArrowRight"; break;
        case kVK_UpArrow: key = @"ArrowUp"; break;
        case kVK_DownArrow: key = @"ArrowDown"; break;
        case kVK_Delete: key = @"Backspace"; break;
        case kVK_Return: key = @"Enter"; break;
        case kVK_Escape: key = @"Escape"; break;
        case kVK_Space: key = @" "; break;
        default: break;
    }
    if ([event type] == NSEventTypeFlagsChanged) key = @"Shift";
    RootFrame roots(1);
    Rooted<Item> payload(roots, vmap_new());
    ime_set(payload.get(), "type", ime_string([event type] == NSEventTypeFlagsChanged &&
        !([event modifierFlags] & NSEventModifierFlagShift) ? "keyup" : "keydown"));
    ime_set(payload.get(), "key", ime_string([key UTF8String]));
    ime_set(payload.get(), "shiftKey", {.item = b2it(([event modifierFlags] & NSEventModifierFlagShift) != 0)});
    ime_set(payload.get(), "ctrlKey", {.item = b2it(([event modifierFlags] & NSEventModifierFlagControl) != 0)});
    ime_set(payload.get(), "altKey", {.item = b2it(([event modifierFlags] & NSEventModifierFlagOption) != 0)});
    ime_set(payload.get(), "metaKey", {.item = b2it(([event modifierFlags] & NSEventModifierFlagCommand) != 0)});
    if (_expanded) ime_set(payload.get(), "candidate_columns", {.item = i2it(5)});
    return [self dispatch:payload.get() client:client];
}
@end

static bool check_engine(int profile) {
    if (!engine_for_profile(profile)) return false;
    RootFrame roots(3);
    Rooted<Item> settings(roots, vmap_new());
    ime_set(settings.get(), "fuzzy", ime_string("all"));
    ime_set(settings.get(), "page_size", {.item = i2it(7)});
    Item create_args[2] = {rooted_word(g_engine_roots[profile]), settings.get()};
    Rooted<Item> session(roots, ime_call("session", create_args, 2));
    if (item_is_error(session.get())) return false;
    Rooted<Item> event(roots, vmap_new());
    ime_set(event.get(), "type", ime_string("keydown"));
    ime_set(event.get(), "key", ime_string("d"));
    Item key_args[3] = {rooted_word(g_engine_roots[profile]), session.get(), event.get()};
    Item result = ime_call("key", key_args, 3);
    return !item_is_error(result) && fn_len(item_attr(result, "actions")) > 0;
}
static uint64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}
static int compare_ns(const void* left, const void* right) {
    uint64_t a = *(const uint64_t*)left;
    uint64_t b = *(const uint64_t*)right;
    return (a > b) - (a < b);
}
static bool bench_engine(uint64_t load_ns) {
    const int samples = 200;
    uint64_t latency[samples] = {};
    uint64_t call_latency[samples] = {};
    RootFrame roots(5);
    Rooted<Item> settings(roots, vmap_new());
    ime_set(settings.get(), "fuzzy", ime_string("all"));
    ime_set(settings.get(), "page_size", {.item = i2it(7)});
    Rooted<Item> session(roots, ItemNull);
    Rooted<Item> event(roots, vmap_new());
    Rooted<Item> result(roots, ItemNull);
    Rooted<Item> settings_result(roots, ItemNull);
    ime_set(event.get(), "type", ime_string("keydown"));
    for (int at = 0; at < samples; at += 2) {
        Item create_args[2] = {rooted_word(g_engine_roots[0]), settings.get()};
        session.set(ime_call("session", create_args, 2));
        if (item_is_error(session.get())) return false;
        for (int letter = 0; letter < 2; letter++) {
            Item settings_arg = session.get();
            uint64_t call_start = monotonic_ns();
            settings_result.set(ime_call("settings", &settings_arg, 1));
            call_latency[at + letter] = monotonic_ns() - call_start;
            if (item_is_error(settings_result.get())) return false;
            ime_set(event.get(), "key", ime_string(letter ? "a" : "d"));
            Item args[3] = {rooted_word(g_engine_roots[0]), session.get(), event.get()};
            uint64_t start = monotonic_ns();
            result.set(ime_call("key", args, 3));
            latency[at + letter] = monotonic_ns() - start;
            if (item_is_error(result.get())) return false;
            session.set(item_attr(result.get(), "session"));
        }
    }
    qsort(latency, samples, sizeof(uint64_t), compare_ns);
    qsort(call_latency, samples, sizeof(uint64_t), compare_ns);
    struct rusage usage = {};
    getrusage(RUSAGE_SELF, &usage);
    log_notice("macos-ime bench: profiles_load_ms=%.3f key_p50_ms=%.3f key_p95_ms=%.3f call_p50_ms=%.3f peak_rss_bytes=%ld samples=%d",
        (double)load_ns / 1000000.0, (double)latency[99] / 1000000.0,
        (double)latency[189] / 1000000.0, (double)call_latency[99] / 1000000.0,
        usage.ru_maxrss, samples);
    return true;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        uint64_t boot_start = monotonic_ns();
        if (!prepare_runtime()) {
            log_error("macos-ime: failed to initialize bundled Lambda Chinese engine");
            return 1;
        }
        if (argc == 2 && (strcmp(argv[1], "--check") == 0 ||
                strcmp(argv[1], "--bench") == 0)) {
            bool ready = check_engine(0) && check_engine(1);
            if (ready && strcmp(argv[1], "--bench") == 0)
                ready = bench_engine(monotonic_ns() - boot_start);
            heap_unregister_gc_root_range(g_edit_roots);
            heap_unregister_gc_root_range(g_base_roots);
            heap_unregister_gc_root_range(g_engine_roots);
            runtime_cleanup(&g_runtime);
            return ready ? 0 : 1;
        }
        [NSApplication sharedApplication];
        g_server = [[IMKServer alloc] initWithName:@"LambdaChineseIME_Connection"
            bundleIdentifier:[NSString stringWithUTF8String:kBundleId]];
        if (!g_server) return 1;
        g_panel = [[IMKCandidates alloc] initWithServer:g_server
            panelType:kIMKSingleRowSteppingCandidatePanel];
        [g_panel setSelectionKeys:@[@18, @19, @20, @21, @23, @22, @26]];
        [g_panel setAttributes:@{IMKCandidatesSendServerKeyEventFirst:@YES,
            NSFontAttributeName:[NSFont systemFontOfSize:16]}];
        [NSApp run];
        return 0;
    }
}
