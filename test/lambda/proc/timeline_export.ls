import t: lambda.chart.timeline
import radiant
pn main() {
    let spec={id:"export-fixture",view:"cards",title:"History",events:[
        {id:"bce",at:{year:-43,precision:"year"},title:"Ancient",metadata:{exact:i64("9007199254740993")}},
        {id:"modern",at:"2020-01",title:"A \"quoted\" title"}]};
    let kinds=["json","csv","xlsx","html","svg","png","jpeg","pdf"];
    let saved=[for (kind in kinds) t.save(spec,"temp/timeline-export." ++ kind,kind,{width:640,height:400},
        {unsupported:"text",fields:["id","at","title","metadata"],table:kind=="pdf",pagination:kind=="pdf"})];
    let json=input("temp/timeline-export.json",'json')^;
    let csv=input("temp/timeline-export.csv",'csv')^;
    let workbook=content(input("temp/timeline-export.xlsx",'zip')^)^;
    let xl=content([for (member in workbook where member.name=="xl") member][0])^;
    let worksheets=content([for (member in xl where member.name=="worksheets") member][0])^;
    let sheet=[for (member in worksheets where member.name=="sheet1.xml") member][0];
    let sheet_xml=input(sheet,'text')^;
    let png=input("temp/timeline-export.png",'binary')^;
    let jpeg=input("temp/timeline-export.jpeg",'binary')^;
    let pdf=input("temp/timeline-export.pdf",'binary')^;
    let film={id:"recorded-film",events:[{id:"film",title:"Recorded frame",media:[{src:"film.mp4",width:100,height:60}]}]};
    let frame=t.export_document(film,"svg",{width:640,height:400},{media_frames:{film:[png]}});
    let failed=radiant.export_document("<html><body>Write failure</body></html>","temp/missing-timeline-dir/export.png",
        {format:"png",width:100,height:100});
    let failed_jpeg=radiant.export_document("<html><body>Write failure</body></html>","temp/missing-timeline-dir/export.jpeg",
        {format:"jpeg",width:100,height:100});
    [
        len(saved)==8 and all([for (path in saved) path is string]),
        json.events[0].at=={year:-43,precision:"year"} and json.events[0].metadata.exact==i64("9007199254740993"),
        len(csv)==2 and csv[1].title=="A \"quoted\" title",
        contains(sheet_xml,"9007199254740993") and contains(sheet_xml,"inlineStr"),
        slice(png,0,8)==b'\x89504E470D0A1A0A',
        slice(jpeg,0,3)==b'\xFFD8FF',
        slice(pdf,0,4)==b'\x25504446',
        contains(input("temp/timeline-export.html",'text')^,"data-timeline-snapshot"),
        contains(input("temp/timeline-export.svg",'text')^,"<svg"),
        failed is string and failed_jpeg is string,
        radiant.export_document("<p>Invalid</p>","temp/timeline-invalid.png",{format:"png",width:0,height:10}) is string,
        radiant.export_document("<p>Invalid</p>","temp/timeline-invalid.png",{format:"unknown",width:10,height:10}) is string,
        frame is element and contains(format(frame,'xml'),"data:image/png;base64,") and contains(format(frame,'xml'),"<image")
    ]
}
