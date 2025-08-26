// Test schema that reproduces the original crash scenario
// This recreates the conflicting definitions that caused the cookbook crash

// First definition of CookbookType
type CookbookType = <cookbook
    title: string,
    author: string,
    year: string?,
    introduction: string,
    recipes: RecipeType+
>

// Document structure - this creates the duplicate/conflict
type Document = <cookbook
    title: string,
    author: string,
    year: string,
    xmlns: string?,
    RecipeType*
>

// Recipe type
type RecipeType = <recipe
    id: string,
    difficulty: string,
    servings: string?,
    name: string,
    description: string
>
