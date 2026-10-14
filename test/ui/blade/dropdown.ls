import ui: lambda.ui.blade

fn choices(id) => <blade.select id:id,default_value:'warden',options:[
    {value:'warden',label:"Dawnwarden"},{value:'ranger',label:"Ranger"},
    {value:'mage',label:"Arcanist"},{value:'locked',label:"Unavailable",disabled:true}],style:"width:260px;">
apply(<blade.page
    <blade.flex direction:'vertical',align:"stretch",gap:32,
        <blade.panel title:"Dark wood",ui.render(choices("dark"))^>
        <blade.panel title:"Parchment",material:'parchment',ui.render(choices("paper"))^>>>)
