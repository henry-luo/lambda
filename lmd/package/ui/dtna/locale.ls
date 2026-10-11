import c: lambda.ui.core.component

// explicit immutable messages avoid a mutable ambient locale (S9.1.7).
let messages = {
    ["en-US"]:{previous:"Previous page",next:"Next page",jump_previous:"Previous 5 pages",jump_next:"Next 5 pages",
        page_size:"Page size",items_per_page:"items / page",jump_to:"Go to page",page:"Page",empty_description:"No data",total_prefix:"Total ",total_suffix:" items"},
    ["zh-CN"]:{previous:"上一页",next:"下一页",jump_previous:"向前 5 页",jump_next:"向后 5 页",
        page_size:"每页条数",items_per_page:"条 / 页",jump_to:"跳至",page:"页",empty_description:"暂无数据",total_prefix:"共 ",total_suffix:" 条"}
}
pub fn resolve(locale = "en-US") map^ {
    if (not (locale is string or locale is symbol) or not c.has(messages,c.text(locale)))
        raise c.fail("locale","supported locales are en-US and zh-CN") else messages[c.text(locale)]
}
