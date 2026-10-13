import data: .mod_data
import math

let CAMERA_CLEARANCE = 4.0
let DEFAULT_FOV = 42.0

pub fn initial() => {cell:"plant",part:"nucleus",compare:false,
    peer:data.cells[0].comparison,favorites:[],library:false,yaw:-0.18,pitch:0.18,distance:6.5,
    pan_x:0.0,pan_y:0.0,rotating:false,token:0,load_token:0,last_ms:null,drag:null}

pub fn select_cell(st,id) => if (data.cell(id) == null) st else
    {*:st,cell:id,part:data.cell(id).defaultOrganelle,peer:data.cell(id).comparison}
pub fn clamp(value,low,high) => min(high,max(low,value))
pub fn orbit(st,x,y) => {*:st,yaw:st.yaw+x,pitch:clamp(st.pitch+y,-1.3,1.3)}
pub fn zoom(st,factor) => {*:st,distance:clamp(st.distance*factor,0.05,18.0)}
// Continue with optical zoom at the dolly limit so the camera stays outside the specimen.
pub fn fov(st) => if (st.distance >= CAMERA_CLEARANCE) DEFAULT_FOV else
    360.0/math.pi*math.atan(math.tan(DEFAULT_FOV*math.pi/360.0)*st.distance/CAMERA_CLEARANCE)
pub fn reset(st) {
    let defaults = initial();
    {*:st,yaw:defaults.yaw,pitch:defaults.pitch,distance:defaults.distance,
        pan_x:0.0,pan_y:0.0,rotating:false,drag:null}
}
pub fn camera(st) {
    let distance = max(CAMERA_CLEARANCE,st.distance);
    [st.pan_x+distance*cos(st.pitch)*sin(st.yaw),
        st.pan_y+distance*sin(st.pitch),distance*cos(st.pitch)*cos(st.yaw)]
}
pub fn matches(st,cell) => not st.library or (contains(st.favorites,cell.id) or false)
pub fn favorite(st) => {*:st,favorites:if (contains(st.favorites,st.cell))
    [for (id in st.favorites where id != st.cell) id] else [*st.favorites,st.cell]}
