// Simple test for Document type content parsing
type RecipeType = <recipe name: string>

type Document = <cookbook
    title: string;                    // required attribute  
    RecipeType+                       // should be parsed as content
>
