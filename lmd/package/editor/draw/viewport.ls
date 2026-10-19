// DOM handlers supply committed native bounds; the coordinate math stays pure (S12.1.1v3).
pub fn point(bounds,zoom,x,y) => {x:(x-bounds.x)/zoom,y:(y-bounds.y)/zoom}
pub fn fit(page,viewport,padding=24.0) => max(0.1,min(3.0,min((viewport.width-padding*2)/page.width,(viewport.height-padding*2)/page.height)))
