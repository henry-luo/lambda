import picker: lambda.ui.workspace.font_picker

<html <head <style picker.css>>
    <body id:"workspace-host",
        picker.wrap(<select id:"first-family",<option value:"Georgia",selected:"","Georgia">;<option value:"Arial","Arial">>,"first-family","Georgia",false,"first-font");
        picker.wrap(<select id:"second-family",<option value:"Georgia",selected:"","Georgia">;<option value:"Arial","Arial">>,"second-family","Georgia",false,"second-font");
        picker.widget("workspace-host","first-font");
        picker.widget("workspace-host","second-font")>>
