// static qualified component names share descriptor construction (S2.4.3v3).
import factory: lambda.ui.core.element

let tags = {
    ["page"]:(attributes,children) => <bold.page *:attributes,*children>,
    ["config-provider"]:(attributes,children) => <bold.config_provider *:attributes,*children>,
    ["button"]:(attributes,children) => <bold.button *:attributes,*children>,
    ["input"]:(attributes,children) => <bold.input *:attributes,*children>,
    ["text-area"]:(attributes,children) => <bold.text_area *:attributes,*children>,
    ["select"]:(attributes,children) => <bold.select *:attributes,*children>,
    ["checkbox"]:(attributes,children) => <bold.checkbox *:attributes,*children>,
    ["radio"]:(attributes,children) => <bold.radio *:attributes,*children>,
    ["switch"]:(attributes,children) => <bold.switch *:attributes,*children>,
    ["title"]:(attributes,children) => <bold.title *:attributes,*children>,
    ["text"]:(attributes,children) => <bold.text *:attributes,*children>,
    ["paragraph"]:(attributes,children) => <bold.paragraph *:attributes,*children>,
    ["link"]:(attributes,children) => <bold.link *:attributes,*children>,
    ["card"]:(attributes,children) => <bold.card *:attributes,*children>,
    ["badge"]:(attributes,children) => <bold.badge *:attributes,*children>,
    ["alert"]:(attributes,children) => <bold.alert *:attributes,*children>,
    ["divider"]:(attributes,children) => <bold.divider *:attributes,*children>,
    ["flex"]:(attributes,children) => <bold.flex *:attributes,*children>,
    ["space"]:(attributes,children) => <bold.space *:attributes,*children>,
    ["form"]:(attributes,children) => <bold.form *:attributes,*children>,
    ["form-item"]:(attributes,children) => <bold.form_item *:attributes,*children>,
    ["progress"]:(attributes,children) => <bold.progress *:attributes,*children>
}

pub fn create(kind,attributes,children) => factory.create(tags,kind,attributes,children)
