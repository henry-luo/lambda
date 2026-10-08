// Test: Object Pipeline
// Layer: 4 | Category: integration | Covers: object types, pipe, query, transform

// ===== Define types =====
type Product {
    name: string,
    price: float,
    category: string,
    fn is_expensive() => ~.price > 50.0
}

// ===== Create instances =====
let products = [
    <Product name: "Laptop", price: 999.99, category: "electronics">,
    <Product name: "Book", price: 15.50, category: "books">,
    <Product name: "Headphones", price: 79.99, category: "electronics">,
    <Product name: "Pen", price: 2.99, category: "office">,
    <Product name: "Monitor", price: 349.00, category: "electronics">,
    <Product name: "Notebook", price: 5.99, category: "office">
]

// ===== Pipeline: filter expensive =====
let expensive = products |: ~.is_expensive()
len(expensive)
expensive |> ~.name

// ===== Pipeline: group by category =====
let electronics = products |: ~.category == "electronics"
len(electronics)
electronics |> ~.name

// ===== Pipeline: calculate stats =====
let prices = products |> ~.price
prices |> sum()
prices |> avg()
prices |> min()
prices |> max()

// ===== Pipeline: sort by price =====
products |> sort((p) => p.price) |> ~.name

// ===== Pipeline: transform to element tree =====
let catalog = <catalog
    for (p in products)
        <product name: p.name, price: string(p.price), p.category>
>
catalog?<product> |> ~.name
count(catalog?<product>)

// ===== Pipeline: find specific product =====
products |: ~.name == "Laptop" |> ~.price

// ===== Combined transform =====
products
    |: ~.price < 100.0
    |> ~.name ++ ": $" ++ string(~.price)
