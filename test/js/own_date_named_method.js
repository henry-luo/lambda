const calendar = {
    value: '',
    setDate(value) {
        this.value = value;
        return this.value;
    }
};

console.log('own-date-named-method:', calendar.setDate('2025-02-14'), calendar.value);
