// D6.3.3/S9.1.2: private module values survive calls and detach on writes.
import data: ._mir_mvp_module_values
pn main() {
    var rows = data.records()
    rows[0].value = 99
    print(rows[0].value == 99); print("\n")
    print(data.records()[0].value == 11); print("\n")
    print(data.nested_value() == 3.75); print("\n")
    print(data.text() == "staged"); print("\n")
    var forwarded = data.forwarded()
    forwarded[1].value = 77
    print(forwarded[1].value == 77); print("\n")
    print(data.records()[1].value == 29); print("\n")
    let read = data.reader()
    var dynamic = read()
    dynamic[0].value = 55
    print(dynamic[0].value == 55); print("\n")
    print(data.records()[0].value == 11); print("\n")
    var propagated = data.propagated()^
    propagated[0].value = 44
    print(propagated[0].value == 44); print("\n")
    print(data.records()[0].value == 11); print("\n")
    let read_capture = data.captured_reader()
    var captured = read_capture()
    captured[0].value = 66
    print(captured[0].value == 66); print("\n")
    print(read_capture()[0].value == 41)
}
