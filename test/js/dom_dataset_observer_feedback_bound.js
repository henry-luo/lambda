var target = document.getElementById("target");
var deliveries = 0;
var record_count = 0;
var feedback_writes = 0;
var observed_names = [];
var observed_old_values = [];
// A retained dataset wrapper must keep its owner alive across precise collection.
var retained_dataset = target.dataset;
gc();
var observer = new MutationObserver(function (records) {
  deliveries++;
  record_count += records.length;
  for (var i = 0; i < records.length; i++) {
    observed_names.push(String(records[i].attributeName));
    observed_old_values.push(String(records[i].oldValue));
    if (records[i].attributeName !== "data-empty" && feedback_writes < 4) {
      feedback_writes++;
      target.dataset.empty = target.dataset.empty === "true" ? "false" : "true";
    }
  }
});

observer.observe(target, {attributes: true, attributeOldValue: true});
retained_dataset.empty = "true";
gc();

setTimeout(function () {
  observer.disconnect();
  console.log("deliveries:" + deliveries);
  console.log("records:" + record_count);
  console.log("names:" + observed_names.join(","));
  console.log("old:" + observed_old_values.join(","));
  console.log("feedback:" + feedback_writes);
  console.log("value:" + target.getAttribute("data-empty"));

  var toggle_observer = new MutationObserver(function () {});
  toggle_observer.observe(target, {attributes: true, attributeOldValue: true});
  target.toggleAttribute("data-enabled");
  var toggle_records = toggle_observer.takeRecords();
  console.log("toggle-records:" + toggle_records.length);
  console.log("toggle-name:" + toggle_records[0].attributeName);
  console.log("toggle-old:" + String(toggle_records[0].oldValue));
  toggle_observer.disconnect();
}, 0);
