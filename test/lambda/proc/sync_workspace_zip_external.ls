import z: lambda.sync.workspace_zip
import w: lambda.sync.workspace
import p: lambda.sync.protocol

pn main() {
    let path = "temp/sync-workspace-external.zip"
    let base_tree = <fs kind:'dir',
        <fs name:"folder",kind:'dir',<fs name:"a.txt",kind:'file',"base">>
        <fs name:"b.txt",kind:'file',"base-b">
    >
    output(base_tree,path,{format:'zip',deterministic:true})^
    let base = z.read_file("external","seed",path)^
    let a = [for (member in base.base.members where member.path == "folder/a.txt") member][0]
    let folder = [for (member in base.base.members where member.path == "folder") member][0]
    let local = w.content("local")^
    let live = z.update(base,w.transact(base.endpoint,"local",[
        {kind:'publish',entry:a.id,content:local.reference}
    ])^.endpoint,[local.object])^
    let external_tree = <fs kind:'dir',
        <fs name:"renamed",kind:'dir',<fs name:"a.txt",kind:'file',"base">>
        <fs name:"b.txt",kind:'file',"external-b">
    >
    output(external_tree,path,{format:'zip',deterministic:true})^
    let imported = z.refresh_file(live,"import","external",[{entry:folder.id,path:"renamed"}])^
    let repeated = z.import_external(imported,"import","external",base.base.id,input(path)^,[{entry:folder.id,path:"renamed"}])^
    let prepared = z.prepare_save(imported)^
    let roundtrip = z.load(z.seal(imported)^,"restart")^
    let same_file_change = <fs kind:'dir',
        <fs name:"folder",kind:'dir',<fs name:"a.txt",kind:'file',"external-a">>
        <fs name:"b.txt",kind:'file',"base-b">
    >
    output(same_file_change,path,{format:'zip',deterministic:true})^
    let overlap = z.import_external(live,"overlap","other",base.base.id,input(path)^)^
    let type_change = <fs kind:'dir',<fs name:"folder",kind:'dir',<fs name:"a.txt",kind:'dir'>>>
    output(type_change,path,{format:'zip',deterministic:true})^
    let review = z.import_external(live,"review","review-actor",base.base.id,input(path)^)^;
    [w.path(imported.endpoint.replica,a.id)^ == "renamed/a.txt",
     w.entry(imported.endpoint.replica,a.id)^.heads[0].content == local.reference,
     imported == repeated,z.dirty(imported)^,roundtrip.session == imported,
     len(w.entry(overlap.endpoint.replica,a.id)^.heads)^ == 2,
     z.prepare_save(overlap) ^ { ^.message == "sync ZIP: resolve file revisions before saving" },
     len(review.conflicts) == 1,review.endpoint == live.endpoint,
     review.imports[0].capture.members != null,
     z.commit_save(imported,{*:prepared,base_id:base.base.id}) ^ { ^.message == "sync ZIP: disk base changed during save" },
     p.next(imported.endpoint,w.have(base.endpoint)^)^.kind == 'transaction',
     z.validate(z.load(z.seal(review)^,"review-restart")^.session)^]
}
