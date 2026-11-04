// Shared table style data and cell appearance; document transactions stay with the caller.
pub let styles = [
    {id:"grid",label:"Simple grid",head:"#edf3f6",ink:"#294f70",band:"#f1f5f9",border:"#cbd5e1"},
    {id:"soft",label:"Banded blue",head:"#dbeafe",ink:"#1e40af",band:"#eff6ff",border:"#bfdbfe"},
    {id:"contrast",label:"Dark header",head:"#294f70",ink:"#ffffff",band:"#edf3f8",border:"#b8c9d9"},
    {id:"plain",label:"Minimal lines",head:"#ffffff",ink:"#334155",band:"#f8fafc",border:"#cbd5e1",minimal:true},
    {id:"blue",label:"Blue accent",head:"#2563eb",ink:"#ffffff",band:"#eff6ff",border:"#93c5fd"},
    {id:"teal",label:"Teal accent",head:"#0f766e",ink:"#ffffff",band:"#f0fdfa",border:"#99d5ce"},
    {id:"purple",label:"Purple accent",head:"#7c3aed",ink:"#ffffff",band:"#f5f3ff",border:"#c4b5fd"},
    {id:"orange",label:"Orange accent",head:"#c2410c",ink:"#ffffff",band:"#fff7ed",border:"#fdba74"},
    {id:"gold",label:"Gold accent",head:"#facc15",ink:"#713f12",band:"#fefce8",border:"#e9cf6a"},
    {id:"rose",label:"Rose accent",head:"#be185d",ink:"#ffffff",band:"#fff1f2",border:"#f9a8d4"}]
pub fn style(id) => [for (entry in styles where entry.id==id) entry][0]
pub fn cell_paint(p,row,col,rows,cols) {
    let s = style(p.style) or styles[0]
    let header = p.header and row==0
    let emphasized = (p.total and row==rows-1) or (p.first and col==0) or (p.last and col==cols-1)
    let band = (p.bandedRows and row%2==1) or (p.bandedColumns and col%2==1);
    {fill:if (header) s.head else if (band or emphasized) s.band else "#ffffff",
        color:if (header) s.ink else "#334155",weight:if (header or emphasized) 600 else 400,border:s.border}
}
