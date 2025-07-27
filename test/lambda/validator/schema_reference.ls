// Schema for type references - demonstrates forward references and complex relationships
type Profile = {
    name: string,
    email: string,
    age: int
}

type Post = {
    id: int,
    title: string,
    content: string,
    published: bool
}

type User = {
    id: int,
    profile: Profile,
    posts: Post*
}

type ReferenceTypes = {
    user: User
}
