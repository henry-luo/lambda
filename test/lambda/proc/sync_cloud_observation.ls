import local: lambda.sync.local
import crypto: lambda.io.crypto
pn main() {
    let area = "temp/sync-cloud-observation-" ++ string(sys.proc.self.pid)
    io.mkdir(area ++ "/tree")^
    let handle = local.create(area ++ "/repo",crypto.key()^,"observation",area ++ "/tree","device")^
    let initial = local.status(handle)^
    let accepted = local.record_cloud_control(handle,"provider",3,"control-three","backup-three")^
    let rollback_result = local.record_cloud_control(handle,"provider",2,"control-two","rollback-backup") ^ { ^ }
    let fork_result = local.record_cloud_control(handle,"provider",3,"fork","fork-backup") ^ { ^ }
    let after = local.status(handle)^
    let repeated = local.record_cloud_control(handle,"provider",3,"control-three","backup-three")^
    let upgraded = local.record_cloud_control(handle,"provider",4,"control-four")^
    local.record_catalog_backup(handle,"provider","backup-four")^
    let final = local.status(handle)^
    let checks = [accepted.generation == initial.generation+1,
        accepted.state.cloud_catalogs == [{id:"provider",snapshot:"backup-three"}],
        rollback_result is error,fork_result is error,after.id == accepted.id,
        repeated.id == accepted.id,upgraded.state.cloud_catalogs == accepted.state.cloud_catalogs,
        final.state.cloud_catalogs == [{id:"provider",snapshot:"backup-four"}],local.verify(handle)^.verified]
    io.delete(area)^
    return all(checks)
}
