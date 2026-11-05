// D7.2.2/S13.1.1v3: imported task targets retain their public boxed entry.
import .conc_worker_mod

pn main() {
    let direct = start(imported_worker, [23])
    print(wait(direct)^); print("\n")
    let operation = imported_worker
    let dynamic = start(operation, [31])
    print(wait(dynamic)^); print("\n")
}
