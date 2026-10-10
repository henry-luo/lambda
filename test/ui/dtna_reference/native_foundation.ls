import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"},
    <dtna.space wrap:true,
        <dtna.button variant:'primary', "Primary">
        <dtna.button "Default">
        <dtna.button variant:'dashed', "Dashed">
        <dtna.button variant:'text', "Text">
        <dtna.button variant:'link', "Link">
        <dtna.button disabled:true, "Disabled">
        <dtna.button danger:true, "Danger">
        <dtna.button loading:true, "Loading">>>)
