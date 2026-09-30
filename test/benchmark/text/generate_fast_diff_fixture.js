#!/usr/bin/env node
// Extract the checked-in Node workload's text pairs for the Go and Python ports.
const fs = require("fs");
const path = require("path");

const source = fs.readFileSync(path.join(__dirname, "fast_diff.js"), "utf8");
const marker = "var fast_diff_pairs = ";
const start = source.indexOf(marker);
const end = source.indexOf("\n];", start);
if (start < 0 || end < 0) throw new Error("missing fast_diff_pairs fixture");
const pairs = JSON.parse(source.slice(start + marker.length, end + 2));
fs.writeFileSync(path.join(__dirname, "fast_diff_pairs.json"), JSON.stringify(pairs) + "\n");
