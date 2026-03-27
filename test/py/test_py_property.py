class Temperature:
    def __init__(self, celsius):
        self._celsius = celsius

    @property
    def celsius(self):
        return self._celsius

    @celsius.setter
    def celsius(self, value):
        if value < -273.15:
            raise ValueError("Temperature below absolute zero")
        self._celsius = value

    @property
    def fahrenheit(self):
        return self._celsius * 9 / 5 + 32

    @fahrenheit.setter
    def fahrenheit(self, value):
        self._celsius = (value - 32) * 5 / 9

t = Temperature(25)
print(t.celsius)
print(t.fahrenheit)
t.celsius = 100
print(t.celsius)
print(t.fahrenheit)
t.fahrenheit = 32
print(t.celsius)
print(t.fahrenheit)

class Circle:
    def __init__(self, radius):
        self._radius = radius

    @property
    def radius(self):
        return self._radius

    @radius.setter
    def radius(self, value):
        if value < 0:
            raise ValueError("Radius cannot be negative")
        self._radius = value

    @property
    def area(self):
        return 3.14159 * self._radius * self._radius

c = Circle(5)
print(c.radius)
print(round(c.area, 4))
c.radius = 10
print(c.radius)
print(round(c.area, 4))

class ReadOnly:
    def __init__(self, value):
        self._value = value

    @property
    def value(self):
        return self._value

ro = ReadOnly(42)
print(ro.value)
