import music: lambda.music

// A4 portrait in CSS pixels; five piano systems fill the printable page.
pub let layout = {width:210.0*96.0/25.4,page_height:297.0*96.0/25.4,mode:'pages',scale:0.72}
pub fn score() map^ => music.load("test/music/a4-study.musicxml")^
pub fn player(score) element^ => <html
    <head <style "html,body{margin:0;padding:0;height:100%;overflow:hidden}body{max-width:none}" >>
    <body <main style:"height:100vh;max-width:800px;margin:0 auto",music.player(score,{layout:layout})^>>>
