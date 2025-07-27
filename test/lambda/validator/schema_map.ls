// Schema for map types - demonstrates map_type and nested structures
type Person = {
    name: string,
    age: int,
    active: bool
}

type Metadata = {
    version: string,
    author: string,
    tags: string*,
    created_at: string
}

type Config = {
    debug: bool,
    timeout: int,
    endpoints: {
        api: string,
        auth: string
    }
}

type MapTypes = {
    person: Person,
    metadata: Metadata,
    config: Config
}
