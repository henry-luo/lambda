// static qualified component names share descriptor construction (S2.4.3v3).
import factory: lambda.ui.core.element

let tags = {
    ["page"]:(attributes,children) => <wired.page *:attributes,*children>,
    ["config-provider"]:(attributes,children) => <wired.config_provider *:attributes,*children>,
    ["button"]:(attributes,children) => <wired.button *:attributes,*children>,
    ["input"]:(attributes,children) => <wired.input *:attributes,*children>,
    ["text-area"]:(attributes,children) => <wired.text_area *:attributes,*children>,
    ["select"]:(attributes,children) => <wired.select *:attributes,*children>,
    ["checkbox"]:(attributes,children) => <wired.checkbox *:attributes,*children>,
    ["radio"]:(attributes,children) => <wired.radio *:attributes,*children>,
    ["switch"]:(attributes,children) => <wired.switch *:attributes,*children>,
    ["title"]:(attributes,children) => <wired.title *:attributes,*children>,
    ["text"]:(attributes,children) => <wired.text *:attributes,*children>,
    ["paragraph"]:(attributes,children) => <wired.paragraph *:attributes,*children>,
    ["link"]:(attributes,children) => <wired.link *:attributes,*children>,
    ["card"]:(attributes,children) => <wired.card *:attributes,*children>,
    ["badge"]:(attributes,children) => <wired.badge *:attributes,*children>,
    ["alert"]:(attributes,children) => <wired.alert *:attributes,*children>,
    ["divider"]:(attributes,children) => <wired.divider *:attributes,*children>,
    ["flex"]:(attributes,children) => <wired.flex *:attributes,*children>,
    ["space"]:(attributes,children) => <wired.space *:attributes,*children>,
    ["form"]:(attributes,children) => <wired.form *:attributes,*children>,
    ["form-item"]:(attributes,children) => <wired.form_item *:attributes,*children>,
    ["progress"]:(attributes,children) => <wired.progress *:attributes,*children>
}

pub fn create(kind,attributes,children) => factory.create(tags,kind,attributes,children)
