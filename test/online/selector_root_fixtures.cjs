const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['document storage does not add element ancestors',`
 document.documentElement.lang='en-GB';const html=document.documentElement;
 check(html.parentNode===document);const target=document.createElement('p');target.id='target';document.body.append(target);
 check(target.matches(':lang(en)')&&!target.matches(':not(:lang(en)) #target'));
 check(!html.matches('* > html')&&!target.matches('* > html #target')&&!target.matches('* html #target'));
 check(target.closest(':not(:lang(en))')===null&&html.closest(':not(html)')===null);
 check(document.querySelector(':not(:lang(en)) #target')===null);`],
 ['language cascades follow real ancestors and live mutations',`
 document.documentElement.lang='en-GB';check(document.documentElement.parentNode===document);
 const style=document.createElement('style');style.textContent='.language-target{width:140px;height:30px;font-size:50px}:not(:lang(en)) .language-target{width:110px;font-size:38px}';document.head.append(style);
 const wrapper=document.createElement('section'),target=document.createElement('h1');target.className='language-target';target.textContent='Language';wrapper.append(target);document.body.append(wrapper);
 check(target.getBoundingClientRect().width===140&&parseFloat(getComputedStyle(target).fontSize)===50);
 wrapper.lang='fr';check(target.matches(':not(:lang(en)) .language-target'));
 check(target.getBoundingClientRect().width===110&&parseFloat(getComputedStyle(target).fontSize)===38);
 wrapper.removeAttribute('lang');check(target.getBoundingClientRect().width===140&&parseFloat(getComputedStyle(target).fontSize)===50);`],
 ['detached fragments do not impersonate element ancestors',`
 const root=document.createDocumentFragment(),target=document.createElement('span');target.id='leaf';target.lang='en-GB';root.append(target);
 check(!target.matches('* > #leaf')&&!target.matches(':not(:lang(en)) #leaf'));
 check(target.closest(':not(:lang(en))')===null);
 check(root.querySelector('*')===target&&root.querySelector(':not(:lang(en)) #leaf')===null);
 check(root.querySelector(':scope')===null);`],
 ['shadow fragments preserve their element boundary',`
 const host=document.createElement('section');host.lang='en-GB';document.body.append(host);
 const root=host.attachShadow({mode:'open'}),target=document.createElement('span');target.id='leaf';target.lang='en-GB';root.append(target);
 check(!target.matches('* > #leaf')&&!target.matches(':not(:lang(en)) #leaf'));
 check(target.closest(':not(:lang(en))')===null&&target.closest('section')===null);
 check(root.querySelector('*')===target&&root.querySelector(':not(:lang(en)) #leaf')===null);`],
 ['ordinary element scope and real ancestor matching remain available',`
 const wrapper=document.createElement('section'),target=document.createElement('span');wrapper.lang='fr';target.id='leaf';wrapper.append(target);document.body.append(wrapper);
 check(target.matches('section > #leaf')&&target.matches(':lang(fr) #leaf'));
 check(target.closest('section')===wrapper);
 for(const selector of [':scope > #leaf',':is(:scope,.absent) > #leaf',':where(:scope) > #leaf',':not(:not(:scope)) > #leaf'])check(wrapper.querySelector(selector)===target);`],
];
function fixtureHtml(source){return scriptResultFixture(source,'SELECTOR_ROOT_ERROR','SELECTOR_ROOT_RESULT','',true)}
module.exports={cases,fixtureHtml};
