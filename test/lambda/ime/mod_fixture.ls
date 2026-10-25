import ime: lambda.ime.chinese

pub pn engine() map^ { return ime.load("zh-Hans",{directory:"test/ime/fixture"})^ }
pub fn texts(result) array => [for (item in result.items) item.text]
pub fn item(result,text) => [for (value in result.items where value.text == text) value][0]
