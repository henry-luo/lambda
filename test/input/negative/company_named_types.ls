type Address = {
    street: string,
    city: string,
    zipcode: int
}

type Contact = {
    email: string,
    phone: string,
    address: Address
}

type Employee = {
    firstName: string,
    lastName: string,
    contact: Contact
}

type Company = {
    name: string,
    employee: Employee
}
