// Native layouts expose the selected data fields; the walker owns tree links.
pub type Color : binary & {layout: 'native', binding: 'radiant.Color'} {
    r: u8, g: u8, b: u8, a: u8
}
pub type Spacing : binary & {layout: 'native', binding: 'radiant.Spacing'} {
    top: f32, right: f32, bottom: f32, left: f32
}
pub type Background : binary & {layout: 'native', binding: 'radiant.BackgroundProp'} {
    color: Color
}
pub type Border : binary & {layout: 'native', binding: 'radiant.BorderProp'} {
    width: Spacing,
    top_color: Color, right_color: Color, bottom_color: Color, left_color: Color
}
pub type Boundary : binary & {layout: 'native', binding: 'radiant.BoundaryProp'} {
    background: Background? & {pointer: 'native'},
    border: Border? & {pointer: 'native'},
    padding: Spacing
}
pub type Font : binary & {layout: 'native', binding: 'radiant.FontProp'} {
    font_size: f32, letter_spacing: f32, word_spacing: f32
}
pub type View : binary & {layout: 'native', binding: 'radiant.DomElement'} {
    x: f32, y: f32, width: f32, height: f32,
    bound: Boundary? & {pointer: 'native'},
    font: Font? & {pointer: 'native'}
}
pub type TextRect : binary & {layout: 'native', binding: 'radiant.TextRect'} {
    x: f32, y: f32, width: f32, height: f32,
    start_index: i32, length: i32
}
pub type Matrix : binary & {layout: 'native', binding: 'radiant.RdtMatrix'} {
    e11: f32, e12: f32, e13: f32, e21: f32, e22: f32, e23: f32
}
pub type Context : binary & {layout: 'native', binding: 'radiant.PdfWalkFrame'} {
    x: f32, y: f32, page_width: f32, page_height: f32,
    baseline: f32, font_size: f32, opacity: f32,
    color: Color, matrix: Matrix, text: string?, clip_evenodd: bool, sequence: i32
}

pub fn schemas() => {element: View, text_rect: TextRect, context: Context}
