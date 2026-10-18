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

fn image_data_fragment(image, sequence) map^ {
    if (image == null) fragment("")
    else {
        let resource_name = "R" ++ string(sequence);
        let mask = if (image.alpha == null) [] else [{name: resource_name ++ "Mask", category: "XObject",
            width: image.width, height: image.height, gray: true, bytes: image.alpha}];
        let picture = {name: resource_name, category: "XObject", width: image.width, height: image.height,
            gray: false, bytes: image.pixels, mask: if (image.alpha == null) null else resource_name ++ "Mask"};
        let clip_commands = if (image.clipped) numbers([image.clip_x, image.clip_y, image.clip_width, image.clip_height])^ ++ " re W n\n" else "";
        {content: "q\n" ++ clip_commands ++ numbers([image.display_width, 0, 0, -image.display_height, image.x, image.y + image.display_height])^ ++
            " cm\n/" ++ resource_name ++ " Do\nQ\n", resources: mask ++ [picture]}
    }
}

fn hex_word(value) => [for (factor in [4096, 256, 16, 1])
    "0123456789ABCDEF"[int(floor(float(value) / factor)) % 16]] |> join("")

fn unicode_hex(codepoint) => if (codepoint < 65536) hex_word(codepoint)
    else hex_word(55296 + int(floor(float(codepoint - 65536) / 1024))) ++ hex_word(56320 + (codepoint - 65536) % 1024)

fn font_key(glyph) => "F" ++ string(glyph.font.id) ++
    if (glyph.font.glyph_program) "S" ++ string(int(floor(float(glyph.glyph) / 256))) else ""

fn character_code(character) => if (character.font.glyph_program) slice(hex_word(character.glyph), 2, 4) else hex_word(character.glyph)

fn text_fragment(text, color, sequence, invisible = false) map^ {
    let hidden = invisible or color.a == 0;
    let keys = unique([for (glyph in text.glyphs) font_key(glyph)]);
    let resources = [for (key in keys) {
        let glyphs = [for (glyph in text.glyphs where font_key(glyph) == key) glyph];
        {name: key, category: "Font", face: glyphs[0].font, characters: glyphs}
    }];
    let commands = <file format:'pdf',
        if (text.text != null) { "/Span << /ActualText "; radiant.pdf_value(text.text)^; " >> BDC\n" }
        "BT\n" if (hidden) "3 Tr\n"
        for (glyph in text.glyphs) {
            if (glyph.missing != null and not hidden) "3 Tr\n"
            "/" font_key(glyph) " " util.pdf_num(text.size)^ " Tf\n"
            // the page uses CSS y-down coordinates; font glyph space is y-up.
            "1 0 " util.pdf_num(glyph.shear)^ " -1 " numbers([glyph.x, glyph.y])^ " Tm\n<" character_code(glyph) "> Tj\n"
            if (glyph.missing != null and not hidden) "0 Tr\n"
        }
        "ET\n" if (text.text != null) "EMC\n">;
    let painted = combine([if (hidden) fragment(commands) else colored(color, "R" ++ string(sequence) ++ "Text", commands)^,
        for (index, glyph in text.glyphs where glyph.missing != null and not invisible)
            fill_rect(glyph.missing_color, glyph.missing[0], glyph.missing[1], glyph.missing[2], glyph.missing[3],
                "R" ++ string(sequence) ++ "Missing" ++ string(index))^]);
    {*: painted, resources: resources ++ painted.resources}
}

fn graphic_shapes(shapes, sequence, invisible = false) map^ => combine([for (index, path_shape in shapes) {
    let resource_key = "R" ++ string(sequence) ++ "P" ++ string(index);
    if (path_shape.group != null) {
        let inside = graphic_shapes(path_shape.group, resource_key, invisible)^;
        {content: "q\n/" ++ resource_key ++ "Alpha gs\n/" ++ resource_key ++ "Form Do\nQ\n",
            resources: inside.resources ++ [
                {name: resource_key ++ "Alpha", category: "ExtGState", alpha: path_shape.opacity},
                {name: resource_key ++ "Form", category: "XObject", form: true, bounds: path_shape.bounds,
                    content: inside.content, members: inside.resources}]}
    } else {
        let color = path_shape.color;
        let scope = <file format:'pdf',
            for (clip_shape in path_shape.clips) {
                for (command in clip_shape.commands) path_command(command)^;
                if (clip_shape.evenodd) "W* n\n" else "W n\n"
            }
            numbers(path_shape.matrix)^ " cm\n">;
        if (path_shape.text != null) {
            let text = text_fragment(path_shape.text, color, resource_key, invisible)^;
            {*: text, content: <file format:'pdf', "q\n" scope text.content "Q\n">}
        } else {
        let commands = <file format:'pdf', scope
            if (path_shape.stroke) {
                numbers([float(color.r) / 255.0, float(color.g) / 255.0, float(color.b) / 255.0])^ " RG\n"
                util.pdf_num(path_shape.width)^ " w\n" string(path_shape.cap) " J\n" string(path_shape.join) " j\n"
                util.pdf_num(path_shape.miter)^ " M\n[" numbers(path_shape.dash)^ "] " util.pdf_num(path_shape.phase)^ " d\n"
            }
            for (command in path_shape.commands) path_command(command)^
            if (path_shape.stroke) "S\n" else if (path_shape.evenodd) "f*\n" else "f\n">;
        colored(color, resource_key, commands)^
        }
    }
}])

fn graphic_fragment(ctx) map^ {
    let graphic = radiant.pdf_graphic(ctx)^;
    if (graphic.image != null) {
        let image = image_data_fragment(graphic.image, ctx.sequence)^;
        let placed = if (graphic.matrix == null) image
            else {*: image, content: <file format:'pdf', "q\n" numbers(graphic.matrix)^ " cm\n" image.content "Q\n">};
        combine([placed, graphic_shapes(graphic.texts or [], string(ctx.sequence) ++ "Hidden", true)^])
    } else graphic_shapes(graphic.paths, ctx.sequence)^
}

fn draw(event, node, ctx) any^ {
    if (event == "begin_page") {
        // PDF points are 1/72 inch; the committed node uses 1/96-inch CSS pixels.
        combine([fragment("q\n" ++ numbers([ctx.matrix.e11, ctx.matrix.e21, ctx.matrix.e12, ctx.matrix.e22, ctx.matrix.e13, ctx.matrix.e23])^ ++ " cm\n"),
            fill_rect(ctx.color, 0, 0, ctx.page_width, ctx.page_height, "R" ++ string(ctx.sequence))^])
    }
    else if (event == "end_page") "Q\n"
    else if (event == "begin_effect" or event == "end_effect") ""
    else if (event == "element" or event == "begin_fragment" or event == "end_fragment") ""
    else if (event == "fill") fill_rect(ctx.color, ctx.x, ctx.y, ctx.width, ctx.height, "R" ++ string(ctx.sequence))^
    else if (event == "boundary") boundary(node, ctx)^
    else if (event == "text") {
        text_fragment(radiant.pdf_text_run(ctx)^, ctx.color, ctx.sequence)^
    }
    else if (event == "image") image_data_fragment(radiant.pdf_image(ctx)^, ctx.sequence)^
    else if (event == "graphic") graphic_fragment(ctx)^
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
    else if (resource.category == "Font") {
        if (resource.face.glyph_program) {
            let characters = resource.characters;
            (begin, "<< /Type /Font /Subtype /Type3 /FontBBox [" ++ numbers(resource.bbox)^ ++
                "] /FontMatrix [0.001 0 0 0.001 0 0] /FirstChar 0 /LastChar 255 /Widths [" ++
                ([for (code in range(0, 256, 1)) {
                    let matches = [for (character in characters where character.glyph % 256 == code) character.width];
                    if (len(matches) > 0) util.pdf_num(matches[0])^ else "0"
                }] |> join(" ")) ++ "] /Encoding << /Type /Encoding /Differences [" ++
                ([for (character in characters) string(character.glyph % 256) ++ " /G" ++ string(character.glyph)] |> join(" ")) ++
                "] >> /CharProcs << " ++ ([for (character in characters) "/G" ++ string(character.glyph) ++ " " ++
                    reference(resources, resource.name ++ "G" ++ string(character.glyph), first_resource)^] |> join(" ")) ++
                " >> /Resources << " ++ resource_entries(resource.members, resources, first_resource)^ ++
                " >> /ToUnicode " ++ reference(resources, resource.name ++ "Unicode", first_resource)^ ++ " >>\nendobj\n")
        } else {
        (begin, "<< /Type /Font /Subtype /Type0 /BaseFont /" ++ resource.base_name ++
            " /Encoding /Identity-H /DescendantFonts [" ++ reference(resources, resource.name ++ "CID", first_resource)^ ++
            "] /ToUnicode " ++ reference(resources, resource.name ++ "Unicode", first_resource)^ ++ " >>\nendobj\n")
        }
    }
    else if (resource.category == "CIDFont") {
        let face = resource.face;
        (begin, "<< /Type /Font /Subtype /" ++ (if (face.cff) "CIDFontType0" else "CIDFontType2") ++
            " /BaseFont /" ++ resource.base_name ++ " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>" ++
            " /FontDescriptor " ++ reference(resources, resource.parent ++ "Descriptor", first_resource)^ ++
            (if (face.cff) "" else " /CIDToGIDMap /Identity") ++ " /DW 1000 /W [" ++
            ([for (character in resource.characters) string(character.glyph) ++ " [" ++ util.pdf_num(character.width)^ ++ "]"] |> join(" ")) ++
            "] >>\nendobj\n")
    }
    else if (resource.category == "FontDescriptor") {
        let face = resource.face;
        (begin, "<< /Type /FontDescriptor /FontName /" ++ resource.base_name ++ " /Flags " ++ string(face.flags) ++
            " /FontBBox [" ++ numbers(face.bbox)^ ++ "] /Ascent " ++ util.pdf_num(face.ascent)^ ++
            " /Descent " ++ util.pdf_num(face.descent)^ ++ " /CapHeight " ++ util.pdf_num(face.cap_height)^ ++
            " /ItalicAngle " ++ util.pdf_num(face.italic_angle)^ ++ " /StemV 80 /" ++ (if (face.cff) "FontFile3" else "FontFile2") ++
            " " ++ reference(resources, resource.parent ++ "Program", first_resource)^ ++ " >>\nendobj\n")
    }
    else if (resource.category == "FontProgram" or resource.category == "ToUnicode" or resource.category == "GlyphProgram" or
            (resource.category == "XObject" and resource.form)) {
        let details = if (resource.category == "FontProgram")
            (if (resource.cff) " /Subtype /OpenType" else " /Length1 " ++ string(resource.original_length))
            else if (resource.form) " /Type /XObject /Subtype /Form /FormType 1 /BBox [" ++ numbers(resource.bounds)^ ++
                "] /Group << /S /Transparency /I true /K false /CS /DeviceRGB >> /Resources << " ++
                resource_entries(resource.members, resources, first_resource)^ ++ " >>"
            else "";
        (begin, "<<" ++ details ++ " /Filter /FlateDecode /Length " ++ string(len(resource.bytes)) ++
            " >>\nstream\n", resource.bytes, "\nendstream\nendobj\n")
    }
    else if (resource.category == "XObject") {
        let mask = if (resource.mask == null) "" else " /SMask " ++ reference(resources, resource.mask, first_resource)^;
        let dictionary = "<< /Type /XObject /Subtype /Image /Width " ++ string(resource.width) ++
            " /Height " ++ string(resource.height) ++ " /ColorSpace " ++ (if (resource.gray) "/DeviceGray" else "/DeviceRGB") ++
            " /BitsPerComponent 8 /Filter /FlateDecode" ++ mask ++ " /Length " ++ string(len(resource.bytes)) ++ " >>\nstream\n";
        (begin, dictionary, resource.bytes, "\nendstream\nendobj\n")
    }
    else raise error("PDF output: unknown resource category")
}

fn resource_entries(members, resources, first_resource) string^ => [for (category in ["ExtGState", "XObject", "Font"]) "/" ++ category ++ " << " ++
    ([for (key in unique([for (resource in members where resource.category == category) resource.name]))
        "/" ++ key ++ " " ++ reference(resources, key, first_resource)^] |> join(" ")) ++ " >>"] |> join(" ")

fn page_object(page, index, resources, first_resource, annotations, stream_object) any^ {
    let sheet_number = 3 + index * 2;
    let entries = resource_entries(page.resources, resources, first_resource)^;
    let boxes = if (len(page.boxes) > 0)
        ([for (box in page.boxes) "/" ++ box.name ++ " [" ++ numbers(box.values)^ ++ "]"] |> join(" "))
        else "/MediaBox [0 0 " ++ util.pdf_num(page.width * page.scale)^ ++ " " ++ util.pdf_num(page.height * page.scale)^ ++ "]";
    let annots = [for (annotation in annotations where annotation.page == index) string(annotation.number) ++ " 0 R"] |> join(" ");
    let sheet = string(sheet_number) ++ " 0 obj\n<< /Type /Page /Parent 2 0 R " ++ boxes ++
        (if (page.rotation == null) "" else " /Rotate " ++ string(page.rotation)) ++
        " /Annots [" ++ annots ++ "] /Resources << " ++ entries ++ " >> /Contents " ++ string(sheet_number + 1) ++ " 0 R >>\nendobj\n";
    let stream = string(sheet_number + 1) ++ " 0 obj\n<< /Filter /FlateDecode /Length " ++ string(len(page.content)) ++ " >>\nstream\n";
    if (stream_object) (stream, page.content, "\nendstream\nendobj\n") else sheet
}

// Object numbers and the frozen resource bytes are identical in both passes.
fn object_parts(index, pages, resources, first_resource, catalog, page_tree, annotations, extras) any^ {
    if (index == 0) catalog
    else if (index == 1) page_tree
    else if (index < first_resource - 1) {
        let page_index = int(floor(float(index - 2) / 2.0));
        page_object(pages[page_index], page_index, resources, first_resource, annotations, (index - 2) % 2 == 1)^
    }
    else if (index < first_resource - 1 + len(resources)) resource_object(resources[index - first_resource + 1], resources, index + 1, first_resource)^
    else extras[index - first_resource + 1 - len(resources)]
}

// Resource preparation happens once; measuring and final emission use these exact bytes.
fn prepared_resource(resource) map^ {
    if (resource.form) {*: resource, bytes: radiant.pdf_deflate(binary(resource.content))^}
    else if (resource.category == "XObject" or resource.category == "FontProgram" or resource.category == "ToUnicode" or resource.category == "GlyphProgram")
        {*: resource, bytes: radiant.pdf_deflate(resource.bytes)^}
    else resource
}

fn font_resources(resource) array^ {
    let face = resource.face;
    let base_name = [for (character in face.name) {
        let code = ord(character);
        if ((code >= 65 and code <= 90) or (code >= 97 and code <= 122) or (code >= 48 and code <= 57) or character == "-") character
        else "#" ++ slice(hex_word(code), 2, 4)
    }] |> join("");
    let characters = [for (glyph in unique([for (character in resource.characters) character.glyph]))
        [for (character in resource.characters where character.glyph == glyph) character][0]];
    let mapped = [for (character in characters where character.unicode != null) character];
    let cmap = <file format:'pdf', "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /LambdaUnicode def\n/CMapType 2 def\n1 begincodespacerange\n"
        if (face.glyph_program) "<00> <FF>\n" else "<0000> <FFFF>\n"
        "endcodespacerange\n"
        for (offset in range(0, len(mapped), 100)) {
            let group = slice(mapped, offset, min(offset + 100, len(mapped)));
            string(len(group)); " beginbfchar\n";
            for (character in group) { "<"; character_code(character); "> <"; unicode_hex(character.unicode); ">\n" };
            "endbfchar\n"
        }
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n">;
    let unicode_resource = {name: resource.name ++ "Unicode", category: "ToUnicode", bytes: binary(cmap)};
    if (face.glyph_program) {
        let boxes = [for (character in characters) character.program.bbox];
        let bbox = [min([for (box in boxes) box[0]]), min([for (box in boxes) box[1]]),
            max([for (box in boxes) box[2]]), max([for (box in boxes) box[3]])];
        let programs = [for (character in characters) {
            let key = resource.name ++ "G" ++ string(character.glyph);
            let image = if (character.program.image != null) image_data_fragment(character.program.image, key)^ else fragment("");
            let commands = <file format:'pdf', util.pdf_num(character.width)^ " 0 "
                if (character.program.image != null) "d0\nq\n1 0 0 -1 0 0 cm\n"
                else { numbers(character.program.bbox)^; " d1\n" }
                if (character.program.image != null) { image.content; "Q\n" }
                else { for (command in character.program.commands) path_command(command)^; "f\n" }>;
            image.resources ++ [{name: key, category: "GlyphProgram", bytes: binary(commands)}]
        }];
        let members = [for (program in programs) for (entry in program where entry.category == "XObject") entry];
        [{*: resource, base_name: base_name, characters: characters, members: members, bbox: bbox}, unicode_resource,
            for (program in programs) for (entry in program) entry]
    } else [{*: resource, base_name: base_name, characters: characters},
        {name: resource.name ++ "CID", parent: resource.name, category: "CIDFont", face: face, base_name: base_name, characters: characters},
        {name: resource.name ++ "Descriptor", parent: resource.name, category: "FontDescriptor", face: face, base_name: base_name},
        {name: resource.name ++ "Program", category: "FontProgram", cff: face.cff, original_length: len(face.bytes), bytes: face.bytes},
        unicode_resource]
}

// Name-tree keys sort by their decoded PDF string bytes, including UTF-16 surrogate pairs.
fn name_key(text) {
    let codepoints = [for (character in text) ord(character)];
    if (all([for (cp in codepoints) cp < 128])) codepoints
    else [254, 255, for (cp in codepoints) for (unit in if (cp < 65536) [cp]
        else [55296 + int(floor(float(cp - 65536) / 1024.0)), 56320 + (cp - 65536) % 1024])
        for (byte in [int(floor(float(unit) / 256.0)), unit % 256]) byte]
}

fn outline_parent(outlines, index) {
    let candidates = [for (i in range(0, index, 1) where outlines[i].level < outlines[index].level) i];
    if (len(candidates) == 0) -1 else candidates[len(candidates) - 1]
}

fn outline_object(outlines, index, root_number) any^ {
    let parent = outline_parent(outlines, index);
    let siblings = [for (i in range(0, len(outlines), 1) where outline_parent(outlines, i) == parent) i];
    let position = [for (i, sibling in siblings where sibling == index) i][0];
    let children = [for (i in range(index + 1, len(outlines), 1) where outline_parent(outlines, i) == index) i];
    let endings = [for (i in range(index + 1, len(outlines), 1) where outlines[i].level <= outlines[index].level) i];
    let ending = if (len(endings) == 0) len(outlines) else endings[0];
    <file format:'pdf', string(root_number + index + 1) " 0 obj\n<< /Title " radiant.pdf_value(outlines[index].title)^
        " /Dest " radiant.pdf_value(outlines[index].destination)^ " /Parent "
        string(if (parent < 0) root_number else root_number + parent + 1) " 0 R"
        if (position > 0) " /Prev " ++ string(root_number + siblings[position - 1] + 1) ++ " 0 R"
        if (position + 1 < len(siblings)) " /Next " ++ string(root_number + siblings[position + 1] + 1) ++ " 0 R"
        if (len(children) > 0) " /First " ++ string(root_number + children[0] + 1) ++ " 0 R /Last " ++
            string(root_number + children[len(children) - 1] + 1) ++ " 0 R /Count " ++ string(ending - index - 1)
        " >>\nendobj\n">
}

fn annotation_object(annotation, page) element^ {
    let link = annotation.link;
    let internal = starts_with(link.href, "#");
    <file format:'pdf', string(annotation.number) " 0 obj\n<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect "
        radiant.pdf_value([link.x * page.scale, (page.height - link.y - link.height) * page.scale,
            (link.x + link.width) * page.scale, (page.height - link.y) * page.scale])^
        if (internal) {
            " /Dest ";
            radiant.pdf_value(slice(link.href, 1, len(link.href)))^
        } else {
            " /A ";
            radiant.pdf_object({S: 'URI', URI: link.href})^
        }
        " >>\nendobj\n">
}

fn document(plan) element^ {
    let source_pages = if (plan.pages == null) [plan] else plan.pages;
    let pages = [for (page in source_pages) {*: page, content: radiant.pdf_deflate(page.content)^,
        resources: [for (resource in page.resources) prepared_resource(resource)^]}];
    let contributions = [for (page in pages) for (resource in page.resources) resource];
    let fonts = [for (key in unique([for (resource in contributions where resource.category == "Font") resource.name])) {
        let uses = [for (resource in contributions where resource.name == key) resource];
        {*: uses[0], characters: [for (use in uses) for (character in use.characters) character]}
    }];
    let resources = [for (resource in contributions where resource.category != "Font") resource,
        for (font in fonts) for (resource in font_resources(font)^) prepared_resource(resource)^];
    let first_resource = 3 + 2 * len(pages);
    // marked-content ActualText requires PDF 1.5; embedded OpenType/CFF requires 1.6.
    let header = if (any([for (font in fonts) font.face.cff and not font.face.glyph_program])) "%PDF-1.6\n"
        else "%PDF-1.5\n";
    let first_extra = first_resource + len(resources);
    let links = [for (page_index, page in pages) for (link in page.links) {page: page_index, link: link}];
    let outlines = if (plan.outlines == null) [] else plan.outlines;
    let destinations = [for (page_index, page in pages) for (destination in page.destinations)
        {*: destination, page: page_index, x: destination.x * page.scale, y: (page.height - destination.y) * page.scale}];
    let distinct = [for (index, destination in destinations where len([for (previous in range(0, index, 1)
        where destinations[previous].id == destination.id) previous]) == 0) destination];
    let ordered = sort(distinct, (destination) => name_key(destination.id));
    let unresolved = [for (outline in outlines where len([for (destination in distinct where destination.id == outline.destination) destination]) == 0) outline];
    // broken source links keep their painted content without creating dangling PDF references.
    let resolved_links = [for (link in links where not starts_with(link.link.href, "#") or
        len([for (destination in distinct where destination.id == slice(link.link.href, 1, len(link.link.href))) destination]) > 0) link];
    let annotations = [for (index, link in resolved_links) {*: link, number: first_extra + index}];
    let info_number = first_extra + len(annotations);
    let outline_number = info_number + 1;
    let roots = [for (index in range(0, len(outlines), 1) where outline_parent(outlines, index) < 0) index];
    let extras = [for (annotation in annotations) annotation_object(annotation, pages[annotation.page])^] ++
        [<file format:'pdf', string(info_number) " 0 obj\n" radiant.pdf_object(plan.info or {Producer: "Lambda PDF Renderer"})^ "\nendobj\n">] ++
        (if (len(outlines) > 0) [
            <file format:'pdf', string(outline_number) " 0 obj\n<< /Type /Outlines /First " string(outline_number + roots[0] + 1)
                " 0 R /Last " string(outline_number + roots[len(roots) - 1] + 1) " 0 R /Count " string(len(outlines)) " >>\nendobj\n">,
            for (index in range(0, len(outlines), 1)) outline_object(outlines, index, outline_number)^]
        else []);
    let labels = [for (index, page in pages where page.label != null) {index: index, text: page.label}];
    let catalog = <file format:'pdf', "1 0 obj\n<< /Type /Catalog /Pages 2 0 R"
        if (len(outlines) > 0) " /Outlines " ++ string(outline_number) ++ " 0 R /PageMode /UseOutlines"
        if (len(labels) > 0) {
            " /PageLabels << /Nums [";
            for (page_label in labels) { string(page_label.index); " "; radiant.pdf_object({P: page_label.text})^; " " };
            "] >>"
        }
        " /Names << /Dests << /Names ["
        for (destination in ordered) {
            radiant.pdf_value(destination.id)^; " ["; string(3 + destination.page * 2); " 0 R /XYZ ";
            numbers([destination.x, destination.y])^; " null] "
        }
        "] >> >> >>\nendobj\n">;
    let page_tree = "2 0 obj\n<< /Type /Pages /Kids [" ++
        ([for (index in range(0, len(pages), 1)) string(3 + 2 * index) ++ " 0 R"] |> join(" ")) ++
        "] /Count " ++ string(len(pages)) ++ " >>\nendobj\n";
    let object_count = first_resource - 1 + len(resources) + len(extras);
    let lengths = [for (index in range(0, object_count, 1))
        radiant.pdf_measure(object_parts(index, pages, resources, first_resource, catalog, page_tree, annotations, extras)^)^];
    let ends = math.cumsum(lengths);
    let offsets = [for (index in range(0, object_count, 1)) len(header) + if (index == 0) 0 else ends[index - 1]];
    let xref = len(header) + sum(lengths);
    if (len(unresolved) > 0) raise error("PDF output has an unresolved internal destination")
    else if (len(pages) == 0 or xref > 9999999999) raise error("PDF output has no pages or exceeds classic cross-reference offset range")
    else <file format:'pdf', header
        for (index in range(0, object_count, 1)) object_parts(index, pages, resources, first_resource, catalog, page_tree, annotations, extras)^
        "xref\n0 " string(object_count + 1) "\n0000000000 65535 f \n"
        for (offset in offsets) xref_entry(offset)
        "trailer\n<< /Size " string(object_count + 1) " /Root 1 0 R /Info " string(info_number) " 0 R >>\nstartxref\n" string(int(xref)) "\n%%EOF\n">
}

pub fn render(source, options: map) element^ {
    let page = if (source is element)
        radiant.pdf_walk_source(format(source, 'html')^, options, draw, schema.schemas())^
        else radiant.pdf_walk(source, options, draw, schema.schemas())^;
    document(page)^
}

// The CLI supplies an already committed edition; no source reload or layout occurs.
pub fn render_view(source, options: map) element^ =>
    document(radiant.pdf_walk_view(source, options, draw, schema.schemas())^)^
