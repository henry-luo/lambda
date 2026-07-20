global_value = 10

def make_counter():
    count = 5

    def inner():
        nonlocal count
        count = count + 2
        return count

    return inner

counter = make_counter()
print(counter())
print(counter())

def call_immediately():
    value = 5

    def increment():
        nonlocal value
        value = value + 2
        return value

    return increment()

print(call_immediately())
print(global_value)
values = [item for item in [1, 2, 3]]
print(values)
print(global_value)
