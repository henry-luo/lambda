// S9.1.2: metadata edits preserve child order so structural no-ops do not create history.
import img: lambda.image

pub fn containers(image,role) => [for (child in content(image) where name(child) == 'metadata' and child.role == role) child]
pub fn container(image,role) => containers(image,role)[0]
pub fn items(image,role) => [for (node in containers(image,role)) *content(node)]
pub fn replace(image,role,entries) any^ {
    let previous = containers(image,role)
    let updated = <metadata role:role,*entries>;
    if (entries == items(image,role)) image
    else if (len(previous)==0) img.with_children(image,[*content(image),updated])^
    // replace the first slot in place; rebuilding at the end changes otherwise equal snapshots.
    else img.with_children(image,[for (child in content(image) where not (child in previous) or child == previous[0])
        if (child == previous[0]) updated else child])^
}
