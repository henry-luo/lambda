console.log(typeof Intl.NumberFormat + "," +
  typeof Intl.NumberFormat.supportedLocalesOf);
console.log(Intl.NumberFormat.supportedLocalesOf(["fr", "en", "EN"]).join(","));
console.log(Intl.NumberFormat.supportedLocalesOf().length);
console.log(Intl.NumberFormat("en").format(1234.5));
console.log("OK");
