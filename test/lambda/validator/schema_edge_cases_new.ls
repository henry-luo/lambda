// Edge cases schema
type Document = {
    empty_optional: string?,
    nullable: any,
    deeply_nested: {
        level1: {
            level2: {
                value: string
            }
        }
    }
}
