import pipeline: ~~.mod_pipeline
import cases: .mod_performance_cases
let reference=input("test/demo/photo/tests/fixtures/pipeline_reference.json",'json')^
// the fixture was captured before optimization, including transparent, rotated and resized pixels.
all([for (i,recipe in cases.recipes) (
    let actual=pipeline.evaluate(cases.pixels,recipe),
    let expected=reference[i],
    let values=flatten(actual),
    shape(actual)==expected.shape and all([for (j,value in expected.values) abs(values[j]-value)<=1])
)])
pipeline.evaluate(cases.pixels,cases.recipes[0])==cases.pixels
transpose(pipeline.evaluate(cases.pixels,cases.recipes[6]))[3]==transpose(cases.pixels)[3]
let invalid=image_process(cases.pixels,{black:0.8,white:0.4}) ^ { "invalid" }
invalid=="invalid"
let strided=transpose(transpose(cases.pixels))
pipeline.evaluate(strided,cases.recipes[6])==pipeline.evaluate(cases.pixels,cases.recipes[6])
