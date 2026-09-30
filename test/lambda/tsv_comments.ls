let baseline = input('./test/css_cascade_memory_baseline.tsv', 'tsv')^
let baseline_fields = [baseline[0].fixture, baseline[0].phase, baseline[0].document_live, baseline[11].fixture]
len(baseline)
baseline_fields

let tabbed = input('./test/input/commented_rows.tsv', 'tsv')^
let tabbed_fields = [tabbed[0].name, tabbed[1].name, tabbed[2].name, tabbed[2].value]
len(tabbed)
tabbed_fields

let campaign = input('./test/fuzzy/lambda/feature_campaigns.tsv', 'tsv')^
let campaign_fields = [campaign[0].surface, campaign[0].status, campaign[0].formal_refs]
len(campaign)
campaign_fields
