type Company = {
    name: string,
    employee: {
        firstName: string,
        lastName: string,
        contact: {
            email: string,
            phone: string,
            address: {
                street: string,
                city: string,
                zipcode: int
            }
        }
    }
}
