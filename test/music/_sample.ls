import music: lambda.music

// A4 portrait in CSS pixels; five piano systems fill the printable page.
pub let layout = {width:210.0*96.0/25.4,page_height:297.0*96.0/25.4,mode:'pages',scale:0.72}
pub fn score() map^ => music.load("test/music/a4-study.musicxml")^
