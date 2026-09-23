// regression: clock-reading builtins are procedural and return the documented
// datetime precision.
pn main() {
    print(now() is datetime)
    print("\n")
    print(now() is time)
    print("\n")
    print(today() is date)
    print("\n")
    print(today().hour == 0 and today().minute == 0 and today().second == 0)
}
