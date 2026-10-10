import ui: lambda.ui.dtna
view font_scopes: <font_scopes> state font_size:16 {
    <main style:"width:500px;",*[for (node in [
        <dtna.title id:"font-default", "Default">,
        <dtna.config_provider tokens:{font_size:20,primary:"#722ed1"}, *[
            <dtna.title id:"font-large", "Large">,
            <dtna.title id:"font-large-five",level:5, "Large five">,
            <dtna.config_provider tokens:{radius:0}, <dtna.title id:"font-inherited", "Inherited">>,
            <dtna.config_provider tokens:{font_size:12}, *[
                <dtna.title id:"font-small", "Small">,
                <dtna.title id:"font-small-five",level:5, "Small five">]>]>,
        <dtna.config_provider tokens:{font_size:font_size}, *[
            <dtna.title id:"font-dynamic",level:3, "Dynamic">,
            <dtna.input id:"font-draft",default_value:"seed">]>,
        <dtna.title id:"font-sibling",level:5, "Sibling">]) apply(node)]>
}
on ui_change(action) {
    if (action.id == "font-draft") { font_size = if (len(action.value) > 4) 20 else 16 }
}
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <font_scopes>>)
