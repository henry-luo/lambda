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
    // [] is the homogeneous-array form; bare [Contact] is a one-item tuple.
    contacts: Contact[]
}

type Company = {
    name: string,
    employees: Employee[]
}
