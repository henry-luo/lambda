// TeX82 math_spacing: 1=conditional thin, 2=thin, 3=medium, 4=thick.
// A scalar string avoids retaining nested container state between renders.
let table = "0234000122*4000133**3**344*0400400*000000234000111*1111112341011"

pub fn atom_type_index(atom_type) => index_of(
    ["mord", "mop", "mbin", "mrel", "mopen", "mclose", "mpunct", "minner"], atom_type) or 0

// Return fractions of the current math quad (18mu), not the text font's em.
pub fn get_spacing(left_type, right_type, style) {
    let index = atom_type_index(left_type) * 8 + atom_type_index(right_type)
    let code = slice(table, index, index + 1);
    if (code == "2") 3.0 / 18.0
    else if (style == "script" or style == "scriptscript") 0.0
    else if (code == "1") 3.0 / 18.0
    else if (code == "3") 4.0 / 18.0
    else if (code == "4") 5.0 / 18.0
    else 0.0
}
