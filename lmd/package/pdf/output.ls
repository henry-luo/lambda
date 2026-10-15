import radiant
import schema: .view_schema
import util: .util

fn numbers(values) string^ => [for (value in values) util.pdf_num(value)^] |> join(" ")

fn fragment(bytes_value) => {content: bytes_value, resources: []}

fn combine(parts) => {
    content: [for (part in parts) part.content] |> join(""),
    resources: [for (part in parts) for (resource in part.resources) resource]
}

fn colored(color, resource_name, commands) map^ {
    if (color == null or color.a == 0) fragment("")
    else {
        let translucent = color.a != 255;
        let graphics_state = if (translucent) "/" ++ resource_name ++ " gs\n" else "";
        let resources = if (translucent) [{name: resource_name, category: "ExtGState", alpha: float(color.a) / 255.0}] else [];
        {content: "q\n" ++ graphics_state ++ numbers([float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0])^ ++
            " rg\n" ++ commands ++ "Q\n", resources: resources}
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
    else if (event == "element") ""
    else if (event == "boundary") boundary(node, ctx)^
    else if (event == "text") {
        let outline = radiant.pdf_glyph_path(ctx)^;
        colored(ctx.color, "R" ++ string(ctx.sequence),
            ([for (command in outline) path_command(command)^] |> join("")) ++ "f\n")^
    }
    else if (event == "image") image_fragment(ctx)^
    else if (event == "begin_transform") {
        let m = ctx.matrix;
        "q\n" ++ numbers([m.e11, m.e21, m.e12, m.e22, m.e13, m.e23])^ ++ " cm\n"
    }
    else if (event == "end_transform") "Q\n"
    else if (event == "begin_clip") {
        let outline = radiant.pdf_clip_path(ctx)^;
        "q\n" ++ ([for (command in outline) path_command(command)^] |> join("")) ++
            if (ctx.clip_evenodd) "W* n\n" else "W n\n"
    }
    else if (event == "end_clip") "Q\n"
    else raise error("PDF output: unsupported callback " ++ event)
}

fn xref_entry(offset) {
    let digits = string(int(offset));
    slice("0000000000", 0, 10 - len(digits)) ++ digits ++ " 00000 n \n"
}

fn reference(resources, resource_name) string^ {
    let matches = [for (index, resource in resources where resource.name == resource_name) index + 5];
    if (len(matches) != 1) raise error("PDF output: resource reference must resolve uniquely")
    else string(matches[0]) ++ " 0 R"
}

fn resource_object(resource, resources, object_number) array^ {
    let begin = string(object_number) ++ " 0 obj\n";
    if (resource.category == "ExtGState") [begin, "<< /Type /ExtGState /ca " ++ util.pdf_num(resource.alpha)^ ++
        " /CA " ++ util.pdf_num(resource.alpha)^ ++ " >>\nendobj\n"]
    else if (resource.category == "XObject") {
        let mask = if (resource.mask == null) "" else " /SMask " ++ reference(resources, resource.mask)^;
        let dictionary = "<< /Type /XObject /Subtype /Image /Width " ++ string(resource.width) ++
            " /Height " ++ string(resource.height) ++ " /ColorSpace " ++ (if (resource.gray) "/DeviceGray" else "/DeviceRGB") ++
            " /BitsPerComponent 8" ++ mask ++ " /Length " ++ string(len(resource.bytes)) ++ " >>\nstream\n";
        [begin, dictionary, resource.bytes, "\nendstream\nendobj\n"]
    }
    else raise error("PDF output: unknown resource category")
}

fn document(page) element^ {
    let header = "%PDF-1.4\n";
    let catalog = "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    let pages = "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
    let resource_entries = [for (category in ["ExtGState", "XObject"]) "/" ++ category ++ " << " ++
        ([for (index, resource in page.resources where resource.category == category)
            "/" ++ resource.name ++ " " ++ string(index + 5) ++ " 0 R"] |> join(" ")) ++ " >>"] |> join(" ");
    let sheet = "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " ++
        util.pdf_num(page.width * 0.75)^ ++ " " ++ util.pdf_num(page.height * 0.75)^ ++
        "] /Resources << " ++ resource_entries ++ " >> /Contents 4 0 R >>\nendobj\n";
    let stream = "4 0 obj\n<< /Length " ++ string(len(page.content)) ++ " >>\nstream\n";
    let endstream = "\nendstream\nendobj\n";
    let objects = [[catalog], [pages], [sheet], [stream, page.content, endstream]] ++
        [for (index, resource in page.resources) resource_object(resource, page.resources, index + 5)^];
    let lengths = [for (entry in objects) sum([for (part in entry) len(part)])];
    let ends = math.cumsum(lengths);
    let offsets = [for (index in range(0, len(objects), 1)) len(header) + if (index == 0) 0 else ends[index - 1]];
    let xref = len(header) + sum(lengths);
    if (xref > 9999999999) raise error("PDF output exceeds classic cross-reference offset range")
    else <file format:'pdf', header
        for (entry in objects) for (part in entry) part
        "xref\n0 " string(len(objects) + 1) "\n0000000000 65535 f \n"
        for (offset in offsets) xref_entry(offset)
        "trailer\n<< /Size " string(len(objects) + 1) " /Root 1 0 R >>\nstartxref\n" string(int(xref)) "\n%%EOF\n">
}

pub pn render(path: string, options: map) element^ {
    let page = radiant.pdf_walk(path, options, draw, schema.schemas())^;
    return document(page)^
}

pub pn export(path: string, destination: string, options: map) int^ {
    let file = render(path, options)^;
    return output(file, destination, {atomic: true})^
}
