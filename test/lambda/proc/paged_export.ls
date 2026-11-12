import radiant
pn main() {
    let source = input("test/html/paged_export_api.html", 'text')^;
    let jobs = [
        {path:"temp/paged-export-api-all.pdf",options:{format:"pdf",pagination:true,pages:"none"}},
        {path:"temp/paged-export-api-selected.pdf",options:{format:"pdf",pagination:true,pages:"1",export_pages:"3,2,2"}},
        {path:"temp/paged-export-api-thumbnail.png",options:{format:"png",pagination:true,pages:"none",thumbnail_page:2}},
        {path:"temp/paged-export-api-density.png",options:{format:"png",pagination:true,pages:"1",thumbnail_page:2,scale:2}},
        {path:"temp/paged-export-api-preview.png",options:{format:"png",pagination:true,pages:"3,2,2"}},
        {path:"temp/paged-export-api-thumbnail.svg",options:{format:"svg",pagination:true,pages:"none",thumbnail_page:2m}}
    ];
    let written = [for (job in jobs) radiant.export_document(source,job.path,job.options)];
    let original = input(jobs[2].path,'binary')^;
    let invalid = [
        {format:"pdf",pagination:true,pages:2},
        {format:"pdf",pagination:true,export_pages:"2-1"},
        {format:"png",pagination:true,pages:"2,"},
        {format:"pdf",pages:"all"},
        {format:"pdf",export_pages:"2"},
        {format:"png",thumbnail_page:2},
        {format:"png",pagination:true,export_pages:"2"},
        {format:"pdf",pagination:true,thumbnail_page:2},
        {format:"png",pagination:true,thumbnail_page:0},
        {format:"png",pagination:true,thumbnail_page:1.5},
        {format:"png",pagination:true,thumbnail_page:i64("4294967296")},
        {format:"png",pagination:true,thumbnail_page:"2"},
        {format:"png",pagination:true,pages:"4"},
        {format:"pdf",pagination:true,export_pages:"4"},
        {format:"png",pagination:true,thumbnail_page:4},
        {format:"png",pagination:true,thumbnail_page:2.00000000000000000001m}
    ];
    let rejected = [for (i in 0 to (len(invalid)-1)) {
        let destination = "temp/paged-export-api-invalid-" ++ string(i) ++ ".bin";
        output(original,destination)^;
        let result = radiant.export_document(source,destination,invalid[i]);
        result is string and input(destination,'binary')^ == original
    }];
    let all_pdf = input(jobs[0].path,'pdf')^;
    let selected_pdf = input(jobs[1].path,'pdf')^;
    let density = input(jobs[3].path,'binary')^;
    let preview = input(jobs[4].path,'binary')^;
    [all([for (result in written) result == true]),
     len(all_pdf.pages) == 3,
     len(selected_pdf.pages) == 2,
     slice(original,16,24) == b'\x0000006400000078',
     slice(density,16,24) == b'\x000000C8000000F0',
     slice(preview,16,24) == b'\x00000064000000F0',
     contains(input(jobs[5].path,'text')^,"<svg"),
     len(rejected) == 16 and all(rejected)]
}
