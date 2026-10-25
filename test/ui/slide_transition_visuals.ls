import slide: lambda.slide
import html: lambda.slide.html

let effects = [
    {id:"wipe",kind:'wipe',variant:'normal'}, {id:"split",kind:'split',variant:'out'},
    {id:"split-in",kind:'split',variant:'in'}, {id:"bars",kind:'random-bars',variant:'normal'},
    {id:"shape",kind:'shape',variant:'circle'}, {id:"diamond",kind:'shape',variant:'diamond'},
    {id:"uncover",kind:'uncover',variant:'normal'}, {id:"cover",kind:'cover',variant:'normal'},
    {id:"push",kind:'push',variant:'normal'}, {id:"reveal",kind:'reveal',variant:'normal'},
    {id:"fade",kind:'fade',variant:'through-black'}, {id:"cut",kind:'cut',variant:'through-black'}];
<html <head <style html.css ++ "html,body{background:#fff;margin:0}body{display:flex;flex-wrap:wrap;width:960px}.slide-player{height:180px}">>;
    <body for (effect in effects) slide.player(<presentation width:320.0,height:180.0,
        <slide background:"#ff0000"><slide background:"#0000ff",transition:effect.kind,transition_variant:effect.variant,transition_duration:1000.0>>,
        {instance:"fx-" ++ effect.id,width:320.0,height:180.0,controls:false})^>>
