import { data_map } from "./_lambda_data_map.ls";
data_map().then(value => {
    console.log(value.tick);
    console.log(value.empty === null);
    console.log(value.missing === undefined);
    console.log(value.nested.height);
});
