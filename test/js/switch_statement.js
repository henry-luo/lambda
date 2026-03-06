// Switch statement tests

// Switch with integer cases and break
function dayName(n) {
  var name = "";
  switch (n) {
    case 1:
      name = "Monday";
      break;
    case 2:
      name = "Tuesday";
      break;
    case 3:
      name = "Wednesday";
      break;
    case 4:
      name = "Thursday";
      break;
    case 5:
      name = "Friday";
      break;
    default:
      name = "Weekend";
      break;
  }
  return name;
}

// Switch with return (no break needed)
function getColor(code) {
  switch (code) {
    case "r": return "red";
    case "g": return "green";
    case "b": return "blue";
    default: return "unknown";
  }
}

// Switch with expression matching (switch(true))
function classify(score) {
  switch (true) {
    case score >= 90:
      return "A";
    case score >= 80:
      return "B";
    case score >= 70:
      return "C";
    default:
      return "F";
  }
}

// Switch with multiple statements per case
function describe(x) {
  var result = "";
  switch (x) {
    case 0:
      result = "zero";
      result = result + "!";
      break;
    case 1:
      result = "one";
      break;
    default:
      result = "other";
      break;
  }
  return result;
}

const result = {
  day1: dayName(1),
  day3: dayName(3),
  day5: dayName(5),
  day7: dayName(7),
  colorR: getColor("r"),
  colorB: getColor("b"),
  colorX: getColor("x"),
  classA: classify(95),
  classB: classify(85),
  classC: classify(75),
  classF: classify(50),
  desc0: describe(0),
  desc1: describe(1),
  desc9: describe(9)
};
result;
