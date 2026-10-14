// lipsum and blindtext: filler paragraphs (vibe/impl/Lambda_Impl_Latex_Packages.md §7).
// Neither package's text is bundled; the paragraphs cycle through Cicero's de Finibus
// 1.10.32–33, the public-domain source of lorem ipsum, and registry.target_diagnostics
// reports filler-text-approximation.
import util: ~~.util

let PARAGRAPHS = [
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.",
    "Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem.",
    "At vero eos et accusamus et iusto odio dignissimos ducimus qui blanditiis praesentium voluptatum deleniti atque corrupti quos dolores et quas molestias excepturi sint occaecati cupiditate non provident, similique sunt in culpa qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio cumque nihil impedit quo minus id quod maxime placeat facere possimus, omnis voluptas assumenda est, omnis dolor repellendus."]

// lipsum prints paragraphs 1 to 7 by default; blindtext's \Blindtext prints five
let DEFAULT_COUNT = {lipsum: 7, 'lipsum*': 7, blindtext: 1, Blindtext: 5}

pub let COMMANDS = {lipsum: ["lipsum", "lipsum*"], blindtext: ["blindtext", "Blindtext"]}

// the paragraph numbers of \lipsum[a-b] or [n]; the default range otherwise
fn numbers(command, option) {
    let parts = if (option == null) [] else split(trim(option), "-")
    let first = if (len(parts) > 0) (int(trim(parts[0])) ^ { null }) else null
    let final = if (len(parts) > 1) (int(trim(parts[1])) ^ { null }) else first
    if (first == null or final == null or final < first) [for (i in 1 to DEFAULT_COUNT[command]) i]
    else [for (i in first to final) i]
}

// the filler paragraphs a command prints; lipsum's starred form runs them together
pub fn render(el) {
    let command = string(name(el))
    let texts = [for (i in numbers(command, util.optional_raw(el))) PARAGRAPHS[(i - 1) % len(PARAGRAPHS)]]
    if (command == "lipsum*") <span class: "latex-filler", util.str_join(texts, " ")>
    else <div class: "latex-filler", for (text in texts) <p text>>
}
