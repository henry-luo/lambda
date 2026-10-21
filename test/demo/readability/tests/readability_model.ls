// Model checks for the Readability demo: page loading, reader availability,
// per-tab reader mode, appearance settings and keyboard shortcuts.
import model: ~~.mod_model

let pages = model.load_pages("file://" ++ sys.proc.self.cwd# ++ "/test/demo/readability/")^;
[for (p in pages) {id: p.id, readerable: p.readerable, article: p.article != null}];
[for (p in pages where p.readerable) {title: p.article.title, byline: p.article.byline,
    site: p.article.site_name, date: model.date_label(p.article.published_time), minutes: model.minutes(p.article)}]

let st = model.initial()
model.reading(st, pages)
let on = model.toggle(st, pages);
[model.reading(on, pages), on.reader]
// reader mode belongs to the tab: a second tab opens in its original form
let second = model.action(on, pages, "tab", "1");
[second.at, model.reading(second, pages), model.reading(model.select_tab(second, pages, 0), pages)]
// the front page is not readerable, so the toggle does nothing there
let home = model.select_tab(on, pages, 3)
model.toggle(home, pages) == home
model.toggle(on, pages).reader;
[model.action(on, pages, "theme", "sepia").theme, model.action(on, pages, "theme", "neon").theme];
[model.resize_text(st, 1).size, model.resize_text(st, 9).size, model.resize_text(st, -9).size];
[model.action(st, pages, "larger", null).size, model.action(st, pages, "smaller", null).size]
model.reading(model.shortcut(st, pages, "R", true, true), pages)
model.reading(model.shortcut(on, pages, "Escape", false, false), pages)
model.shortcut(st, pages, "Escape", false, false) == st;
[model.shortcut(st, pages, "3", true, false).at, model.shortcut(st, pages, "9", true, false).at]
model.shortcut(st, pages, "r", false, false) == st;
[model.date_label("2026-07-14T08:00:00Z"), model.date_label("2026-03-02T09:30:00+01:00"),
    model.date_label(null), model.date_label("July")]
model.words("  one two\tthree\nfour ")
