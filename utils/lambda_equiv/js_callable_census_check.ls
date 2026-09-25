// Native --check gate for utils/js_callable_census.py.
import .js_callable_census_core
pn main() {
    let report = callable_census()^
    callable_census_print(report)
    callable_census_check(report)
}
