// RelaxNG schema converted to Lambda schema
// Based on cookbook.rng - comprehensive recipe collection schema

// Ingredient with name, amount, and optional unit
type IngredientType = <ingredient
    name: string,                     // ingredient name (required)
    amount: string,                   // ingredient amount (required)
    unit: string?,                    // optional unit of measurement
    optional: string?                 // optional flag for optional ingredients
>

// Cooking step with step number and instructions
type StepType = <step
    number: string,                   // step number as string
    text: string                      // step instructions
>

// Instructions container
type InstructionsType = <instructions
    StepType+                         // one or more instruction steps
>

// Ingredients container
type IngredientsType = <ingredients
    IngredientType+                   // one or more ingredients
>

// Preparation time with unit and duration
type PrepTimeType = <prepTime
    unit: string,                     // unit: "minutes" or "hours"
    text: string                      // duration as text
>

// Recipe with all details as child elements
type RecipeType = <recipe
    id: string,                       // unique recipe ID
    difficulty: string,               // difficulty: "easy", "medium", "hard"
    servings: string?,                // optional number of servings
    name: string,                     // recipe name as child element
    description: string,              // recipe description as child element
    prepTime: PrepTimeType,           // preparation time as child element
    ingredients: IngredientsType,     // ingredients container
    instructions: InstructionsType,   // instructions container
    notes: string?                    // optional cooking notes as child element
>

// Document structure - cookbook root element
type Document = <cookbook
    title: string,                    // cookbook title (required attribute)
    author: string,                   // cookbook author (required attribute)
    year: string,                     // publication year (required attribute)
    xmlns: string?,                   // optional namespace
    introduction: string,             // introduction as child element
    RecipeType*                       // zero or more recipes as child elements
>
