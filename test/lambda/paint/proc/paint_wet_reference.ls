// Reference values for Radiant wet-step parity (PT14, PE9). The native fixture uses the same sheet.
import wc: lambda.procart.watercolor
let Sheet = wc.Sheet
pn main() {
    let w = 32, h = 24, n = w * h
    let raw = [for (i in 0 to (21*n - 1))
        if (i < n) 0.3 + float(i % 7) * 0.05
        else if (i < 2*n and (i % n) % w >= 8 and (i % n) % w < 24 and (i % n) div w >= 6 and (i % n) div w < 18) 0.5
        else if (i == 3*n + 12*w + 16) 0.25
        else if (i == 8*n + 12*w + 16) 0.7 else 0.0]
    var s: Sheet = {width:w,height:h,scale:2.5,wet_diffusion:0.064,pigment_diffusion:0.0288,edge_kernel:3,edge_kernel_large:16,velocity_clamp:0.8,cells:raw,x0:0,x1:w-1,y0:0,y1:h-1,steps:0,since_shrink:0,wet_cells:0}
    let ads: float[] = wc.adsorption(wc.CLASSIC)
    var k: int = 0
    while (k < 10) { wc.step(s, ads, wc.SETTINGS)^; k = k + 1 }
    return [for (p in [1, 3, 4, 8, 11]) int(round(s.cells[p*n + 12*w + 16] * 1000000.0))] ++ [s.wet_cells]
}
