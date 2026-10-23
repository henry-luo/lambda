// A bounded EMF bitmap/text profile replays saved previews as static SVG (S1.8).
import utf16: lambda.io.utf16
import media: lambda.doc.media
import util: lambda.doc.pdf.util

type Real : binary & {endian:'little'} {value:f32}
let byte_weights = [1,256,65536,16777216]
fn uint(bytes, at, count = 4) => if (at < 0 or at+count > len(bytes)) null else
    sum([for (i in range(0,count,1)) int(bytes[at+i])*byte_weights[i]])
fn sint(bytes, at) {
    let value = uint(bytes,at);
    if (value == null) null else if (value >= 2147483648) value-4294967296 else value
}
fn little(value, count = 4) => [for (i in range(0,count,1)) int(floor(value/byte_weights[i])) % 256]
fn real(bytes, at) => (binary_decode(slice(bytes,at,at+4),Real) ^ {null}).value
fn rgb(value) => "rgb(" ++ string(value % 256) ++ "," ++ string(int(floor(value/256)) % 256) ++
    "," ++ string(int(floor(value/65536)) % 256) ++ ")"
fn within(at, size, limit, minimum = 0) => at >= minimum and size >= 0 and at+size <= limit

fn bitmap(bytes) {
    let info = uint(bytes,84)
    let info_size = uint(bytes,88)
    let bits = uint(bytes,92)
    let bits_size = uint(bytes,96)
    let width = sint(bytes,info+4)
    let height = sint(bytes,info+8)
    let x = sint(bytes,44)
    let y = sint(bytes,48)
    let w = sint(bytes,100)
    let h = sint(bytes,104)
    let alpha = int(bytes[43])
    let compression = uint(bytes,info+16)
    let transform = [for (at in [52,56,60,64,68,72]) real(bytes,at)]
    let valid = len(bytes) >= 108 and within(info,info_size,len(bytes),108) and
        within(bits,bits_size,len(bytes),108) and info_size >= 40 and uint(bytes,info) == 40 and
        uint(bytes,info+12,2) == 1 and uint(bytes,info+14,2) == 32 and
        contains([0,3],compression) and (compression == 0 or info_size >= 52 and
            [uint(bytes,info+40),uint(bytes,info+44),uint(bytes,info+48)] == [16711680,65280,255]) and
        width > 0 and abs(height) > 0 and width*abs(height) <= 65536 and
        bits_size >= width*abs(height)*4 and x >= 0 and y >= 0 and w > 0 and h > 0 and
        x+w <= width and y+h <= abs(height) and uint(bytes,80) == 0 and util.is_identity(transform) and
        int(bytes[40]) == 0 and contains([0,1],alpha) and sint(bytes,32) > 0 and sint(bytes,36) > 0;
    if (not valid) null else {
        // EMF alpha pixels are premultiplied; BMP V4 stores straight BGRA with an explicit alpha mask.
        let locations = [for (row in range(0,h,1), column in range(0,w,1))
            bits+((if (height > 0) height-1-y-row else y+row)*width+x+column)*4]
        let valid_pixels = alpha == 0 or all([for (at in locations, channel in [0,1,2])
            int(bytes[at+channel]) <= int(bytes[at+3])])
        let pixels = [for (at in locations,
            let a = if (alpha == 1) int(bytes[at+3]) else 255)
            *[for (channel in range(0,4,1))
                if (channel == 3) int(round(a*int(bytes[42])/255.0))
                else if (alpha == 0) int(bytes[at+channel])
                else if (a == 0) 0 else min(255,int(round(int(bytes[at+channel])*255.0/a)))]]
        let size = 122+w*h*4
        let header = [66,77,*little(size),0,0,0,0,*little(122),*little(108),*little(w),
            *little(4294967296-h),1,0,32,0,*little(3),*little(w*h*4),
            *fill(16,0),*little(16711680),*little(65280),*little(255),*little(4278190080),
            *little(1934772034),*fill(48,0)]
        let uri = media.data_uri(binary(header ++ pixels),"image/bmp")^;
        if (not valid_pixels) null else <image x:string(sint(bytes,24)),y:string(sint(bytes,28)),width:string(sint(bytes,32)),
            height:string(sint(bytes,36)),href:uri,preserveAspectRatio:"none">
    }
}

fn font(bytes) {
    let value = utf16.decode(slice(bytes,40,104),true) ^ {null}
    let height = abs(sint(bytes,12));
    if (len(bytes) < 104 or value == null or height <= 0 or height > 4096 or
        sint(bytes,20) != 0 or sint(bytes,24) != 0) null
    else {family:split(value,chr(0))[0],height:height,weight:sint(bytes,28),italic:int(bytes[32]) != 0}
}
fn text_record(bytes, ctx) {
    let count = uint(bytes,44)
    let at = uint(bytes,48)
    let dx = uint(bytes,72)
    let options = uint(bytes,52)
    let text = if (count > 0 and count <= 1024 and within(at,count*2,len(bytes),76))
        utf16.decode(slice(bytes,at,at+count*2),true) ^ {null} else null
    let advances = if (count > 0 and count <= 1024 and dx != 0 and within(dx,count*4,len(bytes),76))
        [for (i in range(0,count,1)) sint(bytes,dx+4*i)] else []
    let valid = len(bytes) >= 76 and text != null and ctx.font != null and options == 0 and
        contains([0,2,6,24,26,30],ctx.align) and
        (dx == 0 or len(advances) == count) and ctx.background_mode == 1;
    if (not valid) null else {
        let width = sum(advances)
        let horizontal = ctx.align % 8
        let left = sint(bytes,36)-(if (horizontal == 6) width/2.0 else if (horizontal == 2) width else 0.0)
        let y = sint(bytes,40)+(if (ctx.align < 8) ctx.font.height else 0.0)
        let starts = [for (i in range(0,count,1) where i == 0 or
            uint(bytes,at+i*2,2) < 56320 or uint(bytes,at+i*2,2) >= 57344) i]
        let positions = if (dx == 0) string(sint(bytes,36)) else join([for (i in starts)
            string(left+sum(slice(advances,0,i)))]," ")
        let anchor = if (dx != 0 or horizontal == 0) "start" else if (horizontal == 6) "middle" else "end"
        let graphic = <text x:positions,y:string(y),["text-anchor"]:anchor,
            ["font-family"]:ctx.font.family,["font-size"]:string(ctx.font.height),
            ["font-weight"]:string(ctx.font.weight),["font-style"]:if (ctx.font.italic) "italic" else "normal",
            fill:ctx.color,text>;
        {graphic:graphic,text:text}
    }
}

fn walk(bytes, at, ctx, remaining) {
    let kind = uint(bytes,at)
    let size = uint(bytes,at+4);
    if (remaining <= 0 or not within(at,size,len(bytes)) or size < 8 or size % 4 != 0) null
    else {
        let record = slice(bytes,at,at+size)
        let value = if (kind == 114) bitmap(record) else if (kind == 84) text_record(record,ctx) else null
        let created_font = if (kind == 82) font(record) else null
        let selected = uint(record,8)
        let next = if (kind == 9 and size >= 16 and sint(record,8) > 0 and sint(record,12) > 0)
                {*:ctx,width:sint(record,8),height:sint(record,12)}
            else if (kind == 10 and size >= 16) {*:ctx,x:sint(record,8),y:sint(record,12)}
            else if (kind == 24 and size >= 12) {*:ctx,color:rgb(selected)}
            else if (kind == 25 and size >= 12) ctx
            else if (kind == 18 and size >= 12 and selected == 1) {*:ctx,background_mode:selected}
            else if (kind == 22 and size >= 12) {*:ctx,align:selected}
            else if (kind == 82 and created_font != null)
                {*:ctx,fonts:{*:ctx.fonts,[string(selected)]:created_font}}
            else if (kind == 37 and size >= 12) {*:ctx,font:ctx.fonts[string(selected)]}
            else if (kind == 114 and value != null) {*:ctx,elements:ctx.elements ++ [value]}
            else if (kind == 84 and value != null)
                {*:ctx,elements:ctx.elements ++ [value.graphic],labels:ctx.labels ++ [value.text]}
            else if (kind == 14 and size >= 20 and at+size == len(bytes) and remaining == 1) ctx else null;
        if (next == null) null
        else if (kind == 14) next else walk(bytes,at+size,next,remaining-1)
    }
}
pub fn read(bytes, width, height) {
    if (not (bytes is binary) or len(bytes) < 88 or len(bytes) > 8388608 or
        width <= 0 or height <= 0 or width > 16384 or height > 16384) null else {
    let header = uint(bytes,4)
    let valid = uint(bytes,0) == 1 and header >= 88 and header <= len(bytes) and
        uint(bytes,40) == 1179469088 and uint(bytes,48) == len(bytes) and
        uint(bytes,52) > 1 and uint(bytes,52) <= 4096;
    if (not valid) null else {
        let initial = {x:sint(bytes,8),y:sint(bytes,12),width:sint(bytes,16)-sint(bytes,8),
            height:sint(bytes,20)-sint(bytes,12),fonts:{},font:null,color:"#000000",align:0,
            background_mode:1,elements:[],labels:[]}
        let rendered = walk(bytes,header,initial,uint(bytes,52)-1);
        if (rendered == null or rendered.width <= 0 or rendered.height <= 0 or len(rendered.elements) == 0) null
        else {svg:<svg xmlns:"http://www.w3.org/2000/svg",width:string(width),height:string(height),
            viewBox:join([string(rendered.x),string(rendered.y),string(rendered.width),string(rendered.height)]," "),
            *rendered.elements>,text:join(rendered.labels,"\n")}
    }
    }
}
