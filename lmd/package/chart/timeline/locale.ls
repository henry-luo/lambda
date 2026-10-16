// Explicit locale data and pure formatting keep exports independent of the host.
import numbers: lambda.chart.numbers

let translations={
    en:{matches:{one:"{count} matching event",other:"{count} matching events"},cluster_events:{one:"{count} event",other:"{count} events"}},
    zh:{first:"首项",previous:"上一项",next:"下一项",last:"末项",fit:"显示全部",play:"播放",pause:"暂停",resume:"继续",stop:"停止",
        zoom_in:"放大",zoom_out:"缩小",reset:"重置",quick_jump:"跳至事件",layout:"布局",theme:"主题",cards:"卡片",axis:"时间轴",story:"故事",
        light:"浅色",dark:"深色",high_contrast:"高对比度",navigation:"时间线导航",groups:"时间线分组",search:"搜索时间线",
        no_results:"没有匹配的事件",matches:"{count} 个匹配事件",previous_match:"上一个匹配项",next_match:"下一个匹配项",
        filter_groups:"筛选分组",filter_tags:"筛选标签",filter_from:"起始日期",filter_to:"结束日期",clear_filters:"清除筛选",read_more:"展开",read_less:"收起",source_error:"无法加载时间线。",loading:"正在加载时间线…",empty:"没有可显示的事件。",
        bookmark:"事件链接",window_from:"时间窗口起点",window_to:"时间窗口终点",overview:"时间线概览",week_number:"第 {number} 周",
        export_id:"标识",export_display_date:"日期",export_at:"时间",export_from:"起点",export_to:"终点",export_title:"标题",export_subtitle:"副标题",
        export_content:"内容",export_group:"分组",export_subgroup:"子分组",export_tags:"标签",export_metadata:"元数据",export_sheet:"时间线",
        progress:"当前事件进度",overall_progress:"整体时间线进度",move_event:"移动事件",move_group:"将事件移至其他分组",
        resize_start:"调整起点",resize_end:"调整终点",remove_event:"删除事件",pending_edit:"待确认的时间线编辑",
        edit_instructions:"使用方向键调整，按 Enter 应用，按 Escape 取消。",edit_preview:"应用前请检查更改。",
        edit_accept:"应用更改",edit_cancel:"取消更改",cluster_events:"{count} 个事件",compressed_interval:"压缩时间区间",
        inside_compressed:"（位于压缩时间区间内）",visible_events:"当前窗口显示 {visible} / {total} 个事件。使用导航或显示全部以浏览其余事件。"},
    fr:{first:"Premier",previous:"Précédent",next:"Suivant",last:"Dernier",fit:"Tout afficher",play:"Lire",pause:"Pause",resume:"Reprendre",stop:"Arrêter",
        zoom_in:"Agrandir",zoom_out:"Réduire",reset:"Réinitialiser",quick_jump:"Aller à un événement",layout:"Disposition",theme:"Thème",
        cards:"Cartes",axis:"Axe",story:"Récit",light:"Clair",dark:"Sombre",high_contrast:"Contraste élevé",
        navigation:"Navigation chronologique",groups:"Groupes chronologiques",search:"Rechercher dans la chronologie",
        no_results:"Aucun événement correspondant",matches:{one:"{count} événement correspondant",other:"{count} événements correspondants"},previous_match:"Résultat précédent",next_match:"Résultat suivant",
        filter_groups:"Filtrer les groupes",filter_tags:"Filtrer les étiquettes",filter_from:"Depuis",filter_to:"Jusqu’à",clear_filters:"Effacer les filtres",read_more:"En savoir plus",read_less:"Réduire",source_error:"Impossible de charger la chronologie.",loading:"Chargement de la chronologie…",
        empty:"Aucun événement à afficher.",bookmark:"Lien vers l’événement",window_from:"Début de la fenêtre",window_to:"Fin de la fenêtre",overview:"Aperçu chronologique",week_number:"S{number}",
        export_id:"Identifiant",export_display_date:"Date",export_at:"Instant",export_from:"Début",export_to:"Fin",export_title:"Titre",export_subtitle:"Sous-titre",
        export_content:"Contenu",export_group:"Groupe",export_subgroup:"Sous-groupe",export_tags:"Étiquettes",export_metadata:"Métadonnées",export_sheet:"Chronologie",
        progress:"Progression de l’événement",overall_progress:"Progression de la chronologie",move_event:"Déplacer l’événement",move_group:"Déplacer vers un autre groupe",
        resize_start:"Ajuster le début",resize_end:"Ajuster la fin",remove_event:"Supprimer l’événement",pending_edit:"Modification en attente",
        edit_instructions:"Utilisez les flèches pour ajuster, Entrée pour appliquer et Échap pour annuler.",edit_preview:"Vérifiez la modification avant de l’appliquer.",
        edit_accept:"Appliquer",edit_cancel:"Annuler",cluster_events:{one:"{count} événement",other:"{count} événements"},compressed_interval:"Intervalle temporel comprimé",
        inside_compressed:"(dans un intervalle temporel comprimé)",visible_events:"{visible} événements sur {total} dans la fenêtre. Utilisez la navigation ou l’affichage complet pour explorer les autres événements."},
    ar:{first:"الأول",previous:"السابق",next:"التالي",last:"الأخير",fit:"عرض الكل",play:"تشغيل",pause:"إيقاف مؤقت",resume:"استئناف",stop:"إيقاف",
        zoom_in:"تكبير",zoom_out:"تصغير",reset:"إعادة تعيين",quick_jump:"انتقل إلى حدث",layout:"التخطيط",theme:"السمة",
        cards:"بطاقات",axis:"محور زمني",story:"قصة",light:"فاتح",dark:"داكن",high_contrast:"تباين عالٍ",
        navigation:"التنقل في التسلسل الزمني",groups:"مجموعات التسلسل الزمني",search:"البحث في التسلسل الزمني",
        no_results:"لا توجد أحداث مطابقة",matches:{zero:"لا توجد أحداث مطابقة",one:"حدث واحد مطابق",two:"حدثان مطابقان",few:"{count} أحداث مطابقة",many:"{count} حدثًا مطابقًا",other:"{count} حدث مطابق"},previous_match:"النتيجة السابقة",next_match:"النتيجة التالية",
        filter_groups:"تصفية المجموعات",filter_tags:"تصفية الوسوم",filter_from:"من",filter_to:"إلى",clear_filters:"مسح عوامل التصفية",read_more:"عرض المزيد",read_less:"عرض أقل",source_error:"تعذر تحميل التسلسل الزمني.",loading:"جارٍ تحميل التسلسل الزمني…",empty:"لا توجد أحداث للعرض.",
        bookmark:"رابط الحدث",window_from:"بداية النافذة",window_to:"نهاية النافذة",overview:"نظرة عامة على التسلسل الزمني",week_number:"الأسبوع {number}",
        export_id:"المعرّف",export_display_date:"التاريخ",export_at:"الوقت",export_from:"البداية",export_to:"النهاية",export_title:"العنوان",export_subtitle:"العنوان الفرعي",
        export_content:"المحتوى",export_group:"المجموعة",export_subgroup:"المجموعة الفرعية",export_tags:"الوسوم",export_metadata:"البيانات الوصفية",export_sheet:"التسلسل الزمني",
        progress:"تقدم الحدث الحالي",overall_progress:"تقدم التسلسل الزمني",move_event:"نقل الحدث",move_group:"نقل الحدث إلى مجموعة أخرى",
        resize_start:"تعديل البداية",resize_end:"تعديل النهاية",remove_event:"حذف الحدث",pending_edit:"تعديل قيد المراجعة",
        edit_instructions:"استخدم الأسهم للتعديل و Enter للتطبيق و Escape للإلغاء.",edit_preview:"راجع التعديل قبل تطبيقه.",
        edit_accept:"تطبيق التعديل",edit_cancel:"إلغاء التعديل",cluster_events:"{count} أحداث",compressed_interval:"فترة زمنية مضغوطة",
        inside_compressed:"(ضمن فترة زمنية مضغوطة)",visible_events:"{visible} من {total} أحداث في النافذة. استخدم التنقل أو عرض الكل لاستكشاف بقية الأحداث."}
}
let months={
    en:["January","February","March","April","May","June","July","August","September","October","November","December"],
    fr:["janvier","février","mars","avril","mai","juin","juillet","août","septembre","octobre","novembre","décembre"],
    zh:["一月","二月","三月","四月","五月","六月","七月","八月","九月","十月","十一月","十二月"],
    ar:["يناير","فبراير","مارس","أبريل","مايو","يونيو","يوليو","أغسطس","سبتمبر","أكتوبر","نوفمبر","ديسمبر"]}
let short_months={
    en:["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"],
    fr:["janv.","févr.","mars","avr.","mai","juin","juil.","août","sept.","oct.","nov.","déc."],
    zh:["1月","2月","3月","4月","5月","6月","7月","8月","9月","10月","11月","12月"],
    ar:months.ar}
let weekdays={en:["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"],
    fr:["dimanche","lundi","mardi","mercredi","jeudi","vendredi","samedi"],
    zh:["星期日","星期一","星期二","星期三","星期四","星期五","星期六"],
    ar:["الأحد","الاثنين","الثلاثاء","الأربعاء","الخميس","الجمعة","السبت"]}
let era_names={en:{bce:"BCE",ce:"CE"},fr:{bce:"av. J.-C.",ce:"ap. J.-C."},zh:{bce:"公元前",ce:"公元"},ar:{bce:"ق.م.",ce:"م"}}
let tokens=["YYYY","MMMM","dddd","MMM","ddd","SSS","YY","MM","DD","HH","hh","mm","ss","ZZ","GG","M","D","H","h","m","s","A","a","Z","G"]

pub fn language(value = "en-US") {
    let code=lower(split(replace(string(value),"_","-"),"-")[0]);
    if (contains(["en","zh","fr","ar"],code)) code else "en"
}
pub fn direction(value) => if (contains(["ar","fa","he","ur"],lower(split(replace(string(value),"_","-"),"-")[0])) ^ { false }) "rtl" else "ltr"
pub fn message(value, key, fallback) => if (translations[language(value)][key]!=null) translations[language(value)][key] else fallback
pub fn era(value, kind) => era_names[language(value)][kind]
pub fn plural(locale, count) {
    let code=language(locale);
    let value=if (count is number) abs(count) else null;
    let integral=value!=null and floor(value)==value;
    let remainder=if (integral) value%100 else null;
    if (value==null or code=="zh") "other"
    else if (code=="ar") (if (value==0) "zero" else if (value==1) "one" else if (value==2) "two"
        else if (integral and remainder>=3 and remainder<=10) "few" else if (integral and remainder>=11 and remainder<=99) "many" else "other")
    else if (code=="fr") (if (value<2) "one" else if (integral and value%1000000==0) "many" else "other")
    else if (value==1) "one" else "other"
}
pub fn localize_digits(text, locale = "en-US") => if (language(locale)!="ar") text else join([for (index in 0 to len(text)-1,
    let ch=text[index],let at=index_of("0123456789",ch)) if (at==null) ch else "٠١٢٣٤٥٦٧٨٩"[at]],"")
pub fn format_number(value, locale = "en-US", options = {}) {
    let code=language(locale);
    let source=if (options.pattern is string) numbers.format_number(value,options.pattern) else numbers.concise_number(value);
    let negative=starts_with(source,"-");
    let magnitude=if (negative) slice(source,1) else source;
    let grouped=if (options.group==true and not (options.pattern is string) and not contains(lower(magnitude),"e")) numbers.group_digits(magnitude) else magnitude;
    let localized=if (code=="fr") replace(replace(grouped,",","\u202f"),".",",")
        else if (code=="ar") replace(replace(grouped,",","٬"),".","٫") else grouped;
    (if (negative) "-" else "") ++ localize_digits(localized,code)
}
pub fn pad_digits(value, width) {
    let source=string(abs(value));
    (if (value<0) "-" else "") ++ join([for (index in 1 to max(0,width-len(source))) "0"],"") ++ source
}
fn pattern_parts(pattern, values, at = 0) {
    if (at>=len(pattern)) [] else if (pattern[at]=="[" and index_of(slice(pattern,at+1),"]")!=null) {
        let end=at+1+index_of(slice(pattern,at+1),"]");
        [slice(pattern,at+1,end),*pattern_parts(pattern,values,end+1)]
    } else {
        let token=([for (key in tokens where starts_with(slice(pattern,at),key)) key])[0];
        [if (token==null) pattern[at] else values[token],*pattern_parts(pattern,values,at+if (token==null) 1 else len(token))]
    }
}
pub fn format_date(record, pattern, locale = "en-US", offset = 0, labels = {}) {
    let code=language(locale);
    let hour=if (record.hour==null) 0 else record.hour;
    let minutes=if (record.minute==null) 0 else record.minute;
    let seconds=if (record.second==null) 0 else record.second;
    let clock_hour=if (hour%12==0) 12 else hour%12;
    let sign=if (offset<0) "-" else "+";
    let zone=sign ++ pad_digits(abs(offset) div 60,2) ++ ":" ++ pad_digits(abs(offset)%60,2);
    let period=if (hour<12) (if (code=="zh") "上午" else if (code=="ar") "ص" else "AM")
        else if (code=="zh") "下午" else if (code=="ar") "م" else "PM";
    let values={YYYY:pad_digits(record.year,4),YY:pad_digits(abs(record.year)%100,2),MMMM:months[code][record.month-1],MMM:short_months[code][record.month-1],
        MM:pad_digits(record.month,2),M:string(record.month),DD:pad_digits(record.day,2),D:string(record.day),
        dddd:weekdays[code][record.weekday],ddd:slice(weekdays[code][record.weekday],0,3),
        HH:pad_digits(hour,2),H:string(hour),hh:pad_digits(clock_hour,2),h:string(clock_hour),mm:pad_digits(minutes,2),m:string(minutes),
        ss:pad_digits(seconds,2),s:string(seconds),SSS:pad_digits(if (record.millisecond==null) 0 else record.millisecond,3),A:period,a:lower(period),Z:zone,ZZ:replace(zone,":",""),
        G:if (record.year<=0) (if (labels.bce!=null) labels.bce else era(locale,"bce")) else if (labels.ce!=null) labels.ce else era(locale,"ce"),
        GG:if (record.year<=0) (if (labels.bce!=null) labels.bce else era(locale,"bce")) else if (labels.ce!=null) labels.ce else era(locale,"ce")};
    // Literal spans retain authored digits; only date fields use locale digits.
    join(pattern_parts(pattern,map([for (key,value in values) (key,localize_digits(value,code))])),"")
}
