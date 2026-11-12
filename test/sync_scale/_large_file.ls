import local: lambda.sync.local
import repo: lambda.sync.repository
import crypto: lambda.io.crypto
import disk: lambda.io.disk
import fs: lambda.sync.filesystem
import codec: lambda.sync.codec
import workspace: lambda.sync.workspace
import maintenance: lambda.sync.maintenance

pn main() {
    let area = sys.proc.self.env#.LAMBDA_SCALE_AREA
    let agent = local.create(area ++ "/repo.sqlite",crypto.key()^,"large-file",area ++ "/tree","large")^
    local.scan(agent)^
    let snapshot = local.snapshot(agent,"large original")^.state.snapshots[0]
    let initial = local.status(agent)^.state
    let entry = [for (member in initial.disk where member.path == "large.bin") member][0]
    let heads = workspace.entry(initial.endpoint.replica,entry.id)^.heads
    let original = fs.observe(area ++ "/tree","large.bin")^
    let manifest = repo.access(agent.path,agent.keys,pn (tx,k)=> { return repo.get(tx,k,entry.content.manifest,"manifest")^ })^
    let block = disk.capture(area,"replacement-block.bin",65536)^.bytes
    let size = 16777216
    let producer = pn (acc)=> {
        return if (acc.next == 256) {bytes:null,state:acc,done:true}
            else {bytes:acc.bytes,state:{*:acc,next:acc.next+1},done:false}
    }
    let initial_acc = {next:0,bytes:block}
    let digest = crypto.hash_stream({size:size,state:initial_acc},producer)^.hash
    disk.write_stream(area ++ "/tree","large.bin",{size:size,hash:digest,state:initial_acc,expected:original.token},producer)^
    local.scan(agent)^
    io.mkdir(area ++ "/restored")^
    local.restore(agent,snapshot,area ++ "/restored")^
    let restored = fs.observe(area ++ "/restored","large.bin")^
    let verified = local.verify(agent)^
    maintenance.restore_file(agent,entry.id,heads[0].id)^
    let reverted = fs.observe(area ++ "/tree","large.bin")^
    let report = {streamed:manifest.version == 2,pages:len(manifest.pages)^,bytes:entry.content.size,
        snapshot_exact:restored.hash == original.hash,changed:digest != original.hash,
        revision_restore_exact:reverted.hash == original.hash,verified:verified.verified}
    if (not report.streamed or not report.snapshot_exact or not report.changed or
        not report.revision_restore_exact or not report.verified) raise error("scale: inexact large-file workflow")
    output(codec.encode(report)^,
        area ++ "/report.json",{format:'text',atomic:true})^
    return true
}
