// logical source elements present through imported templates (S12.1.3).
import ui: lambda.ui.bold

let gallery = <bold.page title:"Lambda UI · bold",
    <bold.flex direction:'vertical',align:"stretch",gap:28,style:"max-width:1040px;margin:auto;",
        <bold.flex justify:"space-between",wrap:true,
            <bold.badge "λ / BOLD UI">
            <bold.link href:"https://neobrutalism.com/",target:"_blank","Design reference ↗">>
        <bold.flex direction:'vertical',align:"flex-start",gap:12,
            <bold.title level:1,style:"font-size:64px;","Make a bold move.">
            <bold.paragraph style:"max-width:640px;font-size:20px;",
                "Big borders. Flat colors. No quiet corners. A new UI family for Lambda, rendered natively by Radiant.">
            <bold.space wrap:true,gap:16,
                <bold.button id:"gallery-start","Start building →">
                <bold.button variant:'outline',"Explore components">>>
        <bold.card title:"Push the buttons.",description:"Six appearances, three sizes, one unmistakable outline.",
            <bold.flex direction:'vertical',align:"flex-start",gap:24,
                <bold.space wrap:true,gap:16,
                    <bold.button "Default">
                    <bold.button variant:'secondary',"Secondary">
                    <bold.button variant:'outline',"Outline">
                    <bold.button variant:'destructive',"Destructive">
                    <bold.button variant:'ghost',"Ghost">
                    <bold.button variant:'link',"Link">>
                <bold.space wrap:true,gap:16,
                    <bold.button size:'small',"Small">
                    <bold.button "Middle">
                    <bold.button size:'large',"Large">
                    <bold.button disabled:true,"Disabled">>>>
        <bold.flex align:"stretch",gap:24,wrap:true,
            <bold.card title:"Your next big thing.",description:"Native fields. Real keyboard interaction.",style:"flex:1;min-width:280px;",
                <bold.form id:"gallery-form",
                    <bold.form_item label:"Project name",for:"gallery-name",required:true,
                        <bold.input id:"gallery-name",name:"project",placeholder:"Something worth making",required:true>>
                    <bold.form_item label:"Stage",for:"gallery-stage",
                        <bold.select id:"gallery-stage",name:"stage",default_value:'idea',options:[
                            {value:'idea',label:"Just an idea"},{value:'building',label:"Making it happen"},{value:'shipped',label:"Out in the world"}]>>
                    <bold.form_item label:"The pitch",for:"gallery-pitch",
                        <bold.text_area id:"gallery-pitch",rows:3,placeholder:"Tell us what you are building">>
                    <bold.space wrap:true,style:"margin-bottom:24px;",
                        <bold.checkbox id:"gallery-check","Keep me posted">
                        <bold.switch id:"gallery-switch",label:"Publish project">>
                    <bold.button type:'submit',block:true,"Let's make it real →">>>
            <bold.card title:"Loud and clear.",description:"Status has something to say.",style:"flex:1;min-width:280px;",
                <bold.flex direction:'vertical',align:"stretch",gap:20,
                    <bold.alert status:'success',title:"You are all set.","Every great project starts somewhere.">
                    <bold.alert status:'warning',title:"One more thing.","Give your project a name before shipping.">
                    <bold.alert status:'error',title:"Something needs attention.","The message stays clear, even when things go wrong.">
                    <bold.space wrap:true,
                        <bold.badge "New">
                        <bold.badge variant:'secondary',"In progress">
                        <bold.badge variant:'outline',"Coming soon">>
                    <bold.text "On the way to launch">
                    <bold.progress percent:68,label:"Launch progress">>>>
        <bold.card title:"Same confidence. Your colors.",
            <bold.config_provider tokens:{primary:"#fb7185",secondary:"#a5f3fc"},
                <bold.space wrap:true,gap:20,
                    <bold.button id:"gallery-pink","Pink is a power move">
                    <bold.button variant:'secondary',"Cool blue">
                    <bold.config_provider tokens:{radius:0,shadow_offset:6},
                        <bold.button id:"gallery-square","Sharper. Bolder.">>>>
            <bold.paragraph style:"margin:24px 0 0;","Scoped tokens inherit through nested providers. Make it yours.">>
        <bold.divider>
        <bold.space justify:"space-between",wrap:true,
            <bold.text "lambda.ui.bold">
            <bold.text "Built from data. Made to stand out.">>>>;

apply(gallery)
