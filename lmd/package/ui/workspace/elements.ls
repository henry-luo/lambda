// desktop tags use the same neutral factory as the other UI families.
import factory: lambda.ui.core.element

let tags = {
    ["button"]:(a,b) => <workspace.button *:a,*b>,
    ["input"]:(a,b) => <workspace.input *:a,*b>,
    ["text-area"]:(a,b) => <workspace.text_area *:a,*b>,
    ["select"]:(a,b) => <workspace.select *:a,*b>,
    ["checkbox"]:(a,b) => <workspace.checkbox *:a,*b>,
    ["radio"]:(a,b) => <workspace.radio *:a,*b>,
    ["switch"]:(a,b) => <workspace.switch *:a,*b>,
    ["toolbar"]:(a,b) => <workspace.toolbar *:a,*b>,
    ["panes"]:(a,b) => <workspace.panes *:a,*b>,
    ["sidebar"]:(a,b) => <workspace.sidebar *:a,*b>,
    ["content"]:(a,b) => <workspace.content *:a,*b>,
    ["status"]:(a,b) => <workspace.status *:a,*b>
}
pub fn create(kind, props, children) any^ => factory.create(tags,kind,props,children)
