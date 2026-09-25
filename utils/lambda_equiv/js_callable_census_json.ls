// Native --json report for utils/js_callable_census.py.
import .js_callable_census_core
pn main() { print(format(callable_census()^, {type: "json", indent: 2})) }
