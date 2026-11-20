import editor: .mod_editor
import model: .mod_model

// Keep authored navigation and viewport geometry. Text editing and card
// editing each retain the source objects that those event flows can reach.
pub fn document(count=null,keep_text=false,keep_cards=false) any^ {
    let source = editor.DEFAULT_DECK
    let authored = model.scenes(source)^
    let selected = if (count==null) authored else take(authored,count);
    let deck = model.with_slides(source,[for (i,slide in selected)
        if (keep_cards and slide.id=="chapter-4") <slide *:map(slide),*[for (child in content(slide)^
            where (child.id in ["headline","market-total","market-focus"])^) child]>
        else if (keep_text and i<2) slide else <slide *:map(slide),<text role:'title',slide.title>>]);
    editor.document(deck)
}
