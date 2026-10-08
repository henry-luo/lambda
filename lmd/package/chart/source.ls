// Both primary data and lookup data use the same source and value-error contract.
pub fn resolve(data, source = null, datasets = null) {
    if (data != null) data
    else if (source is array) source
    else if (source.values != null) source.values
    else if (source.name != null) (
        let dataset = datasets[source.name],
        if (dataset == null) error("chart: unknown dataset " ++ source.name)
        else if (dataset.values != null) dataset.values else dataset)
    else if (source.url != null) (
        // Convert the input effect into the chart API's value diagnostic (S7.4.1–S7.4.2).
        if (source.format != null) input(source.url, source.format) ^ { ~ }
        else input(source.url) ^ { ~ })
    else []
}
