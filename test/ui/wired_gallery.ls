import ui: lambda.ui.wired

apply(<wired.page title:"Lambda UI · wired",
    <wired.flex direction:'vertical',align:"stretch",gap:28,style:"max-width:1000px;margin:auto;",
        <wired.space justify:"space-between",wrap:true,
            <wired.badge variant:'filled',"λ / WIRED UI">
            <wired.link href:"https://wiredjs.com/",target:"_blank","Inspired by Wired Elements ↗">>
        <wired.flex direction:'vertical',align:"flex-start",gap:12,
            <wired.text style:"color:var(--wired-text-secondary);","A little less polished. A little more possibility.">
            <wired.title level:1,style:"font-size:56px;","Good ideas start with a sketch.">
            <wired.paragraph style:"max-width:680px;font-size:19px;",
                "Pencil lines, paper surfaces, and room to think. Hand-drawn components for Lambda, with real native interaction underneath.">
            <wired.space wrap:true,gap:20,
                <wired.button id:"wired-start",variant:'primary',elevation:3,"Start sketching →">
                <wired.button variant:'text',"Take a look around">>>
        <wired.card title:"Make your mark.",description:"A few strokes can say a lot. Try a button, change its elevation, make it yours.",elevation:2,
            <wired.flex direction:'vertical',align:"flex-start",gap:20,
                <wired.space wrap:true,gap:18,
                    <wired.button "Default">
                    <wired.button variant:'primary',"Primary ink">
                    <wired.button variant:'filled',"Paper fill">
                    <wired.button variant:'text',"Just text">
                    <wired.button disabled:true,"Disabled">>
                <wired.space wrap:true,gap:20,
                    <wired.button size:'small',"Small">
                    <wired.button size:'large',"Large">
                    <wired.button elevation:5,"Elevation five">>>>
        <wired.flex align:"stretch",gap:28,wrap:true,
            <wired.card title:"A notebook for your next idea.",description:"Native fields, labels, and keyboard interaction.",style:"flex:1;min-width:280px;",seed:21,
                <wired.form id:"wired-form",
                    <wired.form_item label:"Working title",for:"wired-name",required:true,
                        <wired.input id:"wired-name",name:"project",placeholder:"An idea worth exploring",required:true>>
                    <wired.form_item label:"Where are you now?",for:"wired-stage",
                        <wired.select id:"wired-stage",name:"stage",default_value:'sketch',options:[
                            {value:'sketch',label:"Just a sketch"},{value:'prototype',label:"Making a prototype"},{value:'ready',label:"Ready to share"}]>>
                    <wired.form_item label:"Notes in the margin",for:"wired-notes",
                        <wired.text_area id:"wired-notes",rows:3,placeholder:"What would you like to make?">>
                    <wired.space wrap:true,style:"margin-bottom:20px;",
                        <wired.checkbox id:"wired-check","Keep me in the loop">
                        <wired.switch id:"wired-switch",label:"Share notebook">>
                    <wired.button type:'submit',block:true,variant:'primary',"Save this idea →">>>
            <wired.card title:"Small signs of progress.",description:"Soft color. Clear messages. Nothing set in stone.",style:"flex:1;min-width:280px;",seed:39,
                <wired.flex direction:'vertical',align:"stretch",gap:20,
                    <wired.alert status:'success',title:"A good beginning.","Your first sketch is ready to share.">
                    <wired.alert status:'warning',title:"One more thought.","Give your idea a title before saving.">
                    <wired.alert status:'error',title:"Let's try that again.","Keep the idea. Check the details.">
                    <wired.space wrap:true,
                        <wired.badge "Draft">
                        <wired.badge variant:'filled',"In the works">
                        <wired.badge variant:'primary',"Ready soon">>
                    <wired.text "From first thought to first prototype">
                    <wired.progress id:"wired-progress",percent:64,label:"Prototype progress">>>>
        <wired.card title:"Your paper. Your pen.",description:"Token overrides inherit through nested scopes.",
            <wired.config_provider tokens:{primary:"#236e65",secondary:"#d8eee8",ink:"#236e65"},
                <wired.space wrap:true,gap:24,
                    <wired.button id:"wired-green",variant:'primary',elevation:2,"Green ink">
                    <wired.button variant:'filled',"Mint paper">
                    <wired.config_provider tokens:{stroke_width:2},
                        <wired.button id:"wired-heavy",variant:'primary',"A heavier pencil">>>>
            <wired.paragraph style:"margin:20px 0 0;color:var(--wired-text-secondary);",
                "The strokes stay stable while you work. Native controls keep focus, editing, and form behavior.">>
        <wired.divider>
        <wired.space justify:"space-between",wrap:true,
            <wired.text "lambda.ui.wired">
            <wired.text "Leave room for the next idea.">>>>)
