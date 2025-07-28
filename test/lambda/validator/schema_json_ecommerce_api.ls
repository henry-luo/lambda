// OpenAPI/Swagger schema converted to Lambda schema
// Based on ecommerce-api.openapi.yml - e-commerce product management API

// Product dimensions
type DimensionsType = {
    length: float?,                   // optional length in centimeters (minimum 0)
    width: float?,                    // optional width in centimeters (minimum 0)
    height: float?,                   // optional height in centimeters (minimum 0)
    weight: float?                    // optional weight in grams (minimum 0)
}

// Category can be string name or int ID (union type for testing)  
type CategoryType = string | int

// Product metadata and timestamps
type ProductMetadataType = {
    createdAt: string?,               // optional creation timestamp (ISO date-time)
    updatedAt: string?,               // optional last update timestamp
    createdBy: string?,               // optional creator username
    version: int?                     // optional version number (minimum 1)
}

// Main product entity
type ProductType = {
    id: int,                          // required unique product ID (minimum 1)
    name: string,                     // required product name (1-200 chars)
    description: string?,             // optional description (max 1000 chars)
    price: float,                     // required price in USD (minimum 0, multiple of 0.01)
    category: CategoryType,           // required category: string name or int ID (union type)
    tags: string*,                    // zero or more tags (max 10) - array type
    dimensions: DimensionsType?,      // optional product dimensions
    inStock: bool,                    // required stock availability flag
    stockQuantity: int?,              // optional stock quantity (minimum 0)
    images: [string]?                 // optional array of image URLs - explicit array syntax
    images: string*,                  // zero or more image URLs (max 5)
    metadata: ProductMetadataType?    // optional metadata
}

// Create product request (subset of Product without ID and metadata)
type CreateProductRequestType = {
    name: string,                     // required product name (1-200 chars)
    description: string?,             // optional description (max 1000 chars) 
    price: float,                     // required price in USD (minimum 0)
    category: string,                 // required category enum
    tags: string*,                    // zero or more tags (max 10)
    dimensions: DimensionsType?,      // optional dimensions
    stockQuantity: int?               // optional stock quantity (minimum 0)
}

// Pagination information
type PaginationInfoType = {
    total: int,                       // required total count (minimum 0)
    limit: int,                       // required items per page (1-100)
    offset: int,                      // required offset (minimum 0)
    hasNext: bool?                    // optional next page indicator
}

// Product list response with pagination
type ProductListResponseType = {
    products: ProductType*,           // zero or more products
    pagination: PaginationInfoType    // required pagination info
}

// Error response for API errors
type ErrorResponseType = {
    error: string,                    // required error code
    message: string,                  // required error message
    details: string*                  // zero or more error details
}
