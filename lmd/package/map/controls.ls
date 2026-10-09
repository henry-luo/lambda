pub fn controls(options) => <div role:"group",'aria-label':"Map navigation",
    for (control in [{action:"zoom-in",label:"Zoom in",text:"+"},{action:"zoom-out",label:"Zoom out",text:"−"},
        {action:"reset",label:"Reset map",text:"Reset"},
        if (options.bounds != null) {action:"fit",label:"Fit map bounds",text:"Fit"}] where control != null)
        <button type:"button",'data-map-action':control.action,'aria-label':control.label,control.text>
>
pub fn features(records) => <ul 'aria-label':"Selected map features",'aria-live':"polite",
    for (record in records) <li if (record.feature.properties.name != null) string(record.feature.properties.name)
        else string(record.feature_id)>
>
