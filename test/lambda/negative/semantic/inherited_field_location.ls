// LR03-41: preserve the existing rejection policy, but locate the written field.
type Animal {
    name: string,
    sound: string = "..."
}
type Dog : Animal {
    sound: string = "woof"
}
null
