const domExpandoCollisionCases = ['assignment', 'descriptor', 'custom-element'];

function domExpandoCollisionHtml(mode) {
    return `<!doctype html><head><style>
        body{margin:0}#probe{width:80px;height:80px;background:red}
        </style></head><body><div id="probe"></div><script>
        const mode=${JSON.stringify(mode)},failures=[];
        let clicks=0,connected=false;
        const button=document.createElement('button');
        function check(name,value){if(!value)failures.push(name)}
        function exercise(node){
            node.appendChild(button);
            for(const key of ['item','toggle','replace']){
                if(mode==='descriptor')Object.defineProperty(node,key,
                    {value:button,writable:true,configurable:true,enumerable:true});
                else node[key]=button;
                check(key+' field retains element',node[key]===button);
                const descriptor=Object.getOwnPropertyDescriptor(node,key);
                check(key+' own descriptor',descriptor && descriptor.value===button && descriptor.writable);
            }
            try{node.toggle.addEventListener('click',function(){clicks++});button.click()}
            catch(error){failures.push('stored button is callable through its field')}
            node.classList.add('first');
            check('token item',node.classList.item(0)==='first');
            check('token toggle',node.classList.toggle('second')===true);
            check('token replace',node.classList.replace('first','third')===true && node.classList.contains('third'));
            check('collection item',node.children.item(0)===button);
            const select=document.createElement('select'),option=document.createElement('option');
            select.appendChild(option);
            check('select item',typeof select.item==='function' && select.item(0)===option);
            check('select options item',select.options.item(0)===option);
            for(const key of ['item','toggle','replace']){
                node[key]=null;check(key+' null field',node[key]===null);
                check(key+' delete',delete node[key]);
                check(key+' no inherited token method',node[key]===undefined && !(key in node));
            }
        }
        if(mode==='custom-element'){
            class FieldControl extends HTMLElement{
                item=null;toggle=null;replace=null;
                connectedCallback(){connected=true;exercise(this)}
            }
            customElements.define('field-control',FieldControl);
            document.body.appendChild(document.createElement('field-control'));
        }else{
            const node=document.createElement('div');document.body.appendChild(node);exercise(node);
        }
        addEventListener('load',function(){
            check('one button click',clicks===1);
            check('custom element connected',mode!=='custom-element'||connected);
            console.log('DOM_EXPANDO_COLLISION_RESULT',JSON.stringify({failures,clicks}));
            document.getElementById('probe').style.background=failures.length?'red':'green';
        });
        </script></body>`;
}

module.exports = {domExpandoCollisionCases, domExpandoCollisionHtml};
