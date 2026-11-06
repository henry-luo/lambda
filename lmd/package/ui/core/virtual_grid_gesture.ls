// S12.1.3v2/D10.2.1: pointer transport produces logical grid coordinates.
import dom
import grid: .virtual_grid

pub pn move(owner,port,gesture,x,y,final,tick,axes,header_width,header_height,selector,event_name) any^ {
    let box=dom.bounding_box(port)
    let metrics=dom.scroll_metrics(port)
    let scroll=dom.scroll_state(port)
    let dx=if (final) 0 else if (x<box.x+24) -28 else if (x>box.x+box.width-24) 28 else 0
    let dy=if (final) 0 else if (y<box.y+24) -28 else if (y>box.y+box.height-24) 28 else 0
    let next_x=max(0,min(metrics.scroll_width-metrics.client_width,scroll.x+dx))
    let next_y=max(0,min(metrics.scroll_height-metrics.client_height,scroll.y+dy))
    if (next_x!=scroll.x or next_y!=scroll.y) {
        // Defer scrolling so a release outside the viewport keeps its final hit.
        if (tick) dom.set_scroll_state(port,next_x,next_y)
        dom.request_frame_once(owner,event_name)
    }
    let target=dom.element_from_point(dom.owner_document(owner),x,y)
    let cell=if (target==null) null else dom.closest(target,selector)
    let hit=if (cell!=null) {row:int(dom.get_attribute(cell,"data-row")),col:int(dom.get_attribute(cell,"data-col"))}
        else if (tick and (dx!=0 or dy!=0)) {
            let local_x=max(0,min(metrics.client_width-header_width,x-box.x-header_width))
            let local_y=max(0,min(metrics.client_height-header_height,y-box.y-header_height));
            grid.hit(axes.rows,axes.columns,next_x+local_x,next_y+local_y)^
        } else null;
    return {gesture:{*:gesture,x:x,y:y},hit:hit}
}
