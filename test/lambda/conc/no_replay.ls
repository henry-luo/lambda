pn tick() {
    print("tick")
    return 5
}
pn leaf(value) {
    sleep(1)^
    return value
}

pn main() {
    print(leaf(tick()))
}
