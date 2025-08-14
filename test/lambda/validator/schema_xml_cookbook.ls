// RelaxNG schema converted to Lambda schema
// Based on cookbook.rng - comprehensive recipe collection schema

// Ingredient with name, amount, and optional unit
type IngredientType = <ingredient
    name: string,                     // ingredient name (required)
    amount: string,                   // ingredient amount (required)
    unit: string?,                    // optional unit of measurement
    optional: bool?                   // optional flag for optional ingredients
>

// Cooking step with step number and instructions
type StepType = <step
    number: int,                      // step number (positiveInteger)
    text: string                      // step instructions
>

// Preparation time with unit and duration
type PrepTimeType = <prepTime
    unit: string,                     // unit: "minutes" or "hours"
    text: string                      // duration as text (positiveInteger)
>

// Recipe with all details
type RecipeType = <recipe
    id: string,                       // unique recipe ID
    difficulty: string,               // difficulty: "easy", "medium", "hard"
    servings: int?,                   // optional number of servings
    name: string,                     // recipe name
    description: string,              // recipe description  
    prepTime: PrepTimeType,           // preparation time
    ingredients: IngredientType+,     // one or more ingredients
    instructions: StepType+,          // one or more instruction steps
    notes: string?                    // optional cooking notes
>

// Cookbook root element
type CookbookType = <cookbook
    title: string,                    // cookbook title
    author: string,                   // cookbook author
    year: string?,                    // optional publication year (gYear)
    introduction: string,             // cookbook introduction
    recipes: RecipeType+              // one or more recipes
>

// Document structure - cookbook is now the root with our parsing fix  
type Document = <cookbook
    title: string,                    // cookbook title (required attribute)
    author: string,                   // cookbook author (required attribute)
    year: string,                     // publication year (required attribute)
    xmlns: string?,                   // optional namespace
    RecipeType*                       // zero or more recipes
>

// Processing instruction for XML declaration
type XmlProcessingInstruction = <?xml
    version: string,                  // XML version
    encoding: string?                 // optional encoding
>
