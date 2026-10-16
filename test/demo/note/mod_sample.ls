// Original sample notebook; the first block of each note supplies its title.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import model: .mod_model

fn heading(words) => node('h1',[text(words)])
fn p(words) => model.paragraph(words)
fn task(words,done=false) => node_attrs('li',[{name:'checked',value:done}],[p(words)])
fn entry(id,folder,title,blocks,stamp,pinned=false) =>
    {id:id,folder:folder,doc:node('doc',[heading(title),*blocks]),updated:stamp,pinned:pinned,deleted:false}

pub fn notebook() => {sequence:20,folders:[{id:"notes",name:"Notes"},{id:"work",name:"Work"},
    {id:"personal",name:"Personal"},{id:"travel",name:"Travel"}],notes:[
    entry("weekend","personal","A slower kind of weekend",[
        p("A few things to make room for. No grand plans, just a little more time for the good stuff."),
        node('h2',[text("Saturday, unhurried")]),
        node('ul',[task("Coffee and a walk by the water",true),task("Stop at the flower market"),
            task("Find something good at the bookshop"),task("Make dinner with whatever looks best")]),
        node('h2',[text("A small reminder")]),
        node('p',[text("Leave the afternoon open. "),text_marked("Not everything needs to be a plan.",[{name:'strong',value:true}])]),
        node('blockquote',[p("Pay attention. Be astonished. Tell about it.")]),
        p("Maybe invite Sam and Jules over on Sunday. Soup, good bread, and the windows open.")],"2026-10-16 09:41",true),
    entry("ideas","work","Little ideas, big possibilities",[
        p("A running list of things worth exploring."),
        node('ul',[node('li',[p("A neighbourhood library for tools and useful things")]),
            node('li',[p("A field guide to our favourite quiet places")]),node('li',[p("Postcards from an ordinary Tuesday")])]),
        p("The best ideas usually start with a conversation.")],"2026-10-15 16:20",true),
    entry("kyoto","travel","Kyoto, someday",[
        p("A week of early walks, small gardens, and very good noodles."),
        node('h2',[text("Places to linger")]),p("Philosopher’s Path before breakfast. The little streets north of Gion. A whole afternoon in a garden."),
        node('h2',[text("Bring along")]),node('ul',[task("A pocket notebook"),task("Comfortable shoes"),task("An extra roll of film")])],"2026-10-15 11:08"),
    entry("meeting","work","Monday / design catch-up",[
        p("Keep it simple. Make the next step obvious."),node('h2',[text("Next steps")]),
        node('ul',[task("Share the first sketches",true),task("Try the smaller navigation"),task("Write down what we learned")])],"2026-10-14 14:30"),
    entry("kitchen","personal","Things from the kitchen",[
        p("Lemon & olive oil cake"),p("200 g flour · 150 g sugar · 3 eggs · 120 ml olive oil · 2 lemons"),
        p("Whisk the eggs and sugar. Add oil, lemon zest, and juice. Fold in the flour. Bake at 175°C until golden."),
        p("Even better the next morning, with coffee.")],"2026-10-13 18:12"),
    entry("reading","notes","The reading list",[
        p("For rainy afternoons and long train rides."),node('ul',[task("A Field Guide to Getting Lost",true),
            task("The Creative Act"),task("Braiding Sweetgrass")]),p("Ask everyone for one book they keep coming back to.")],"2026-10-12 10:05"),
    entry("welcome","notes","Welcome to Notes",[
        p("A home for a thought before it slips away."),p("Make a note, put it in a folder, or pin something you want to keep close. Search finds words throughout your notes."),
        p("Select text to make it bold, italic, or underlined. Use the checklist button to turn a paragraph into a task. Click its circle to mark it complete."),
        p("Save your notebook locally with Command-S. Open it again whenever you like.")],"2026-10-11 08:30")
]}
