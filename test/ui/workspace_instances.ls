import ui: lambda.ui.workspace
import theme: lambda.ui.workspace.tokens
import office: lambda.ui.workspace.office
import ribbon_style: lambda.ui.workspace.ribbon_style
import font_picker: lambda.ui.workspace.font_picker

view <workspace_panel> () any^ state value:"seed", clicks:0 {
    <section class:"workspace-root",style:theme.variables({primary:~.color})^ ++ "height:180px;",
        apply(ui.toolbar({label:"Tools"},ui.button({id:~.button,variant:"primary"},"Save")^)^);
        apply(ui.input({id:~.field,value:value,on_change:"edit"})^);
        <span id:~.count,string(clicks)>>
}
on control_change(req) { value = req.value }
on ui_action(req) { if (req.action == 'click') { clicks = clicks + 1 } }

<html <head <style ui.stylesheet() ++ office.css ++ ribbon_style.css ++ font_picker.css>>
    <body apply(<workspace_panel button:"one-save",field:"one-field",count:"one-count",color:"#102030">);
          apply(<workspace_panel button:"two-save",field:"two-field",count:"two-count",color:"#a04020">);
          <button id:"outside-workspace",class:"workspace-button workspace-variant-primary tool",style:"background-color:#123456;","Document button">;
          <section class:"workspace-root workspace-office",style:theme.variables({primary:"#102030"})^ ++ "height:120px;",
              <button id:"office-one",class:"tool",["aria-pressed"]:"true","One">;
              <div class:"workspace-document",
                  <button id:"document-tool",class:"tool workspace-button",type:"button","Document control">;
                  <span id:"document-picker",class:"font-picker-trigger","Document text">;
                  <div id:"document-dialog",class:"workspace-dialog-backdrop",<div class:"workspace-dialog","Document dialog">>;
                  <div class:"editor-ribbon",<div id:"document-row",class:"home-row","Document row">>>
          >;
          <section class:"workspace-office",style:theme.variables({primary:"#a04020"})^,
              <button id:"office-two",class:"tool",["aria-pressed"]:"true","Two">>;
          <div id:"outside-ribbon",class:"home-row","Document row">;
          <div class:"editor-ribbon",<div id:"inside-ribbon",class:"home-row","Ribbon row">>>>
