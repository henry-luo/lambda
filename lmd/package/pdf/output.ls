import radiant
import schema: .view_schema
import util: .util

fn numbers(values) string^ => [for (value in values) util.pdf_num(value)^] |> join(" ")

fn fragment(bytes_value) => {content: bytes_value, resources: []}

fn combine(parts) => {
    content: <file format:'pdf', for (part in parts) part.content>,
    resources: [for (part in parts) for (resource in part.resources) resource]
}

fn colored(color, resource_name, commands) map^ {
    if (color == null or color.a == 0) fragment("")
    else {
        let translucent = color.a != 255;
        let graphics_state = if (translucent) "/" ++ resource_name ++ " gs\n" else "";
        let resources = if (translucent) [{name: resource_name, category: "ExtGState", alpha: float(color.a) / 255.0}] else [];
        {content: <file format:'pdf', "q\n" graphics_state
            numbers([float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0])^ " rg\n" commands "Q\n">,
            resources: resources}
    }
}

fn fill_rect(color, x, y, width, height, resource_name) map^ {
    if (width <= 0 or height <= 0) fragment("")
    else colored(color, resource_name, numbers([x, y, width, height])^ ++ " re f\n")^
}

fn boundary(node, ctx) map^ {
    let bound = node.bound;
    let resource_name = "R" ++ string(ctx.sequence);
    if (bound == null) fragment("")
    else {
        let bg = bound.background;
        let background = if (bg == null) fragment("") else fill_rect(bg.color, ctx.x, ctx.y, node.width, node.height, resource_name ++ "B")^;
        let border = bound.border;
        if (border == null) background
        else {
            let w = border.width;
            combine([background, fill_rect(border.top_color, ctx.x, ctx.y, node.width, w.top, resource_name ++ "T")^,
                fill_rect(border.right_color, ctx.x + node.width - w.right, ctx.y, w.right, node.height, resource_name ++ "R")^,
                fill_rect(border.bottom_color, ctx.x, ctx.y + node.height - w.bottom, node.width, w.bottom, resource_name ++ "D")^,
                fill_rect(border.left_color, ctx.x, ctx.y, w.left, node.height, resource_name ++ "L")^])
        }
    }
}

fn path_command(command) string^ {
    let op = command[0];
    if (op == "M" or op == "L") numbers([command[1], command[2]])^ ++ if (op == "M") " m\n" else " l\n"
    else if (op == "C") numbers([command[1], command[2], command[3], command[4], command[5], command[6]])^ ++ " c\n"
    else if (op == "Z") "h\n"
    else raise error("PDF output: unsupported outline command")
}

fn image_fragment(ctx) map^ {
    let image = radiant.pdf_image(ctx)^;
    if (image == null) fragment("")
    else {
        let resource_name = "R" ++ string(ctx.sequence);
        let mask = if (image.alpha == null) [] else [{name: resource_name ++ "Mask", category: "XObject",
            width: image.width, height: image.height, gray: true, bytes: image.alpha}];
        let picture = {name: resource_name, category: "XObject", width: image.width, height: image.height,
            gray: false, bytes: image.pixels, mask: if (image.alpha == null) null else resource_name ++ "Mask"};
        let clip_commands = if (image.clipped) numbers([image.clip_x, image.clip_y, image.clip_width, image.clip_height])^ ++ " re W n\n" else "";
        {content: "q\n" ++ clip_commands ++ numbers([image.display_width, 0, 0, -image.display_height, image.x, image.y + image.display_height])^ ++
            " cm\n/" ++ resource_name ++ " Do\nQ\n", resources: mask ++ [picture]}
    }
}

fn draw(event, node, ctx) any^ {
    if (event == "begin_page") {
        // PDF points are 1/72 inch; the committed node uses 1/96-inch CSS pixels.
        combine([fragment("q\n" ++ numbers([0.75, 0, 0, -0.75, 0, ctx.page_height * 0.75])^ ++ " cm\n"),
            fill_rect(ctx.color, 0, 0, ctx.page_width, ctx.page_height, "R" ++ string(ctx.sequence))^])
    }
    else if (event == "end_page") "Q\n"
    else if (event == "element" or event == "begin_fragment" or event == "end_fragment") ""
    else if (event == "fill") fill_rect(ctx.color, ctx.x, ctx.y, ctx.width, ctx.height, "R" ++ string(ctx.sequence))^
    else if (event == "boundary") boundary(node, ctx)^
    else if (event == "text") {
        let outline = radiant.pdf_glyph_path(ctx)^;
        colored(ctx.color, "R" ++ string(ctx.sequence),
            <file format:'pdf', for (command in outline) path_command(command)^ "f\n">)^
    }
    else if (event == "image") image_fragment(ctx)^
    else if (event == "begin_transform") {
        let m = ctx.matrix;
        "q\n" ++ numbers([m.e11, m.e21, m.e12, m.e22, m.e13, m.e23])^ ++ " cm\n"
    }
    else if (event == "end_transform") "Q\n"
    else if (event == "begin_clip") {
        let outline = radiant.pdf_clip_path(ctx)^;
        <file format:'pdf', "q\n" for (command in outline) path_command(command)^
            if (ctx.clip_evenodd) "W* n\n" else "W n\n">
    }
    else if (event == "end_clip") "Q\n"
    else raise error("PDF output: unsupported callback " ++ event)
}

fn xref_entry(offset) {
    let digits = string(int(offset));
    slice("0000000000", 0, 10 - len(digits)) ++ digits ++ " 00000 n \n"
}

fn reference(resources, resource_name, first_resource) string^ {
    let matches = [for (index, resource in resources where resource.name == resource_name) index + first_resource];
    if (len(matches) != 1) raise error("PDF output: resource reference must resolve uniquely")
    else string(matches[0]) ++ " 0 R"
}

fn resource_object(resource, resources, object_number, first_resource) any^ {
    let begin = string(object_number) ++ " 0 obj\n";
    if (resource.category == "ExtGState") (begin, "<< /Type /ExtGState /ca " ++ util.pdf_num(resource.alpha)^ ++
        " /CA " ++ util.pdf_num(resource.alpha)^ ++ " >>\nendobj\n")
    else if (resource.category == "XObject") {
        let mask = if (resource.mask == null) "" else " /SMask " ++ reference(resources, resource.mask, first_resource)^;
        let dictionary = "<< /Type /XObject /Subtype /Image /Width " ++ string(resource.width) ++
            " /Height " ++ string(resource.height) ++ " /ColorSpace " ++ (if (resource.gray) "/DeviceGray" else "/DeviceRGB") ++
            " /BitsPerComponent 8 /Filter /FlateDecode" ++ mask ++ " /Length " ++ string(len(resource.bytes)) ++ " >>\nstream\n";
        (begin, dictionary, resource.bytes, "\nendstream\nendobj\n")
    }
    else raise error("PDF output: unknown resource category")
}

fn page_object(page, index, resources, first_resource, stream_object) any^ {
    let sheet_number = 3 + index * 2;
    let resource_entries = [for (category in ["ExtGState", "XObject"]) "/" ++ category ++ " << " ++
        ([for (resource in page.resources where resource.category == category)
            "/" ++ resource.name ++ " " ++ reference(resources, resource.name, first_resource)^] |> join(" ")) ++ " >>"] |> join(" ");
    let sheet = string(sheet_number) ++ " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " ++
        util.pdf_num(page.width * 0.75)^ ++ " " ++ util.pdf_num(page.height * 0.75)^ ++
        "] /Resources << " ++ resource_entries ++ " >> /Contents " ++ string(sheet_number + 1) ++ " 0 R >>\nendobj\n";
    let stream = string(sheet_number + 1) ++ " 0 obj\n<< /Filter /FlateDecode /Length " ++ string(len(page.content)) ++ " >>\nstream\n";
    if (stream_object) (stream, page.content, "\nendstream\nendobj\n") else sheet
}

// Object numbers and the frozen resource bytes are identical in both passes.
fn object_parts(index, pages, resources, first_resource, catalog, page_tree) any^ {
    if (index == 0) catalog
    else if (index == 1) page_tree
    else if (index < first_resource - 1) {
        let page_index = int(floor(float(index - 2) / 2.0));
        page_object(pages[page_index], page_index, resources, first_resource, (index - 2) % 2 == 1)^
    }
    else resource_object(resources[index - first_resource + 1], resources, index + 1, first_resource)^
}

// Resource preparation happens once; measuring and final emission use these exact bytes.
fn prepared_resource(resource) map^ {
    if (resource.category == "XObject") {*: resource, bytes: radiant.pdf_deflate(resource.bytes)^}
    else resource
}

fn document(plan) element^ {
    let source_pages = if (plan.pages == null) [plan] else plan.pages;
    let pages = [for (page in source_pages) {*: page, content: radiant.pdf_deflate(page.content)^,
        resources: [for (resource in page.resources) prepared_resource(resource)^]}];
    let resources = [for (page in pages) for (resource in page.resources) resource];
    let first_resource = 3 + 2 * len(pages);
    let header = "%PDF-1.4\n";
    let catalog = "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    let page_tree = "2 0 obj\n<< /Type /Pages /Kids [" ++
        ([for (index in range(0, len(pages), 1)) string(3 + 2 * index) ++ " 0 R"] |> join(" ")) ++
        "] /Count " ++ string(len(pages)) ++ " >>\nendobj\n";
    let object_count = first_resource - 1 + len(resources);
    let lengths = [for (index in range(0, object_count, 1))
        radiant.pdf_measure(object_parts(index, pages, resources, first_resource, catalog, page_tree)^)^];
    let ends = math.cumsum(lengths);
    let offsets = [for (index in range(0, object_count, 1)) len(header) + if (index == 0) 0 else ends[index - 1]];
    let xref = len(header) + sum(lengths);
    if (len(pages) == 0 or xref > 9999999999) raise error("PDF output has no pages or exceeds classic cross-reference offset range")
    else <file format:'pdf', header
        for (index in range(0, object_count, 1)) object_parts(index, pages, resources, first_resource, catalog, page_tree)^
        "xref\n0 " string(object_count + 1) "\n0000000000 65535 f \n"
        for (offset in offsets) xref_entry(offset)
        "trailer\n<< /Size " string(object_count + 1) " /Root 1 0 R >>\nstartxref\n" string(int(xref)) "\n%%EOF\n">
}

pub fn render(source, options: map) element^ {
    let page = if (source is element)
        radiant.pdf_walk_source(format(source, 'html')^, options, draw, schema.schemas())^
        else radiant.pdf_walk(source, options, draw, schema.schemas())^;
    document(page)^
}
