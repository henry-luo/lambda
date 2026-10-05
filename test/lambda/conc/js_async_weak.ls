// JSCU25: a JS async activation is weakly live for the Lambda scheduler. One
// the script never awaits still completes before the run ends; one parked on
// a promise nothing will settle does not hold the end-of-run drain open.
import .js_async_weak_module

pn main() {
    tickLater("a")
    parkForever()
    print("main done\n")
}
