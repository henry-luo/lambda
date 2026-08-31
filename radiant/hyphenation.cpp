#include "hyphenation.hpp"

#include <stdint.h>
#include <string.h>

// en-US Liang patterns derived from the ISC-licensed hyphen resource used by
// Lambda's text tests; the data is kept separate from the line-break policy.
static const char g_en_us_pattern_data[] =
    ".ach\0"
    ".adder\0"
    ".aft\0"
    ".alt\0"
    ".amat\0"
    ".anc\0"
    ".ang\0"
    ".anim\0"
    ".ant\0"
    ".ante\0"
    ".antis\0"
    ".ars\0"
    ".artie\0"
    ".arty\0"
    ".asc\0"
    ".asp\0"
    ".ass\0"
    ".aster\0"
    ".atom\0"
    ".aud\0"
    ".avi\0"
    ".awn\0"
    ".bag\0"
    ".bana\0"
    ".base\0"
    ".ber\0"
    ".bera\0"
    ".besm\0"
    ".besto\0"
    ".bri\0"
    ".butti\0"
    ".campe\0"
    ".canc\0"
    ".capab\0"
    ".carol\0"
    ".cat\0"
    ".cela\0"
    ".ch\0"
    ".chilli\0"
    ".ci\0"
    ".citr\0"
    ".coe\0"
    ".congr\0"
    ".cor\0"
    ".corner\0"
    ".demoi\0"
    ".deo\0"
    ".dera\0"
    ".deri\0"
    ".deriva\0"
    ".desc\0"
    ".dictio\0"
    ".dot\0"
    ".driv\0"
    ".duc\0"
    ".dumb\0"
    ".earth\0"
    ".easi\0"
    ".eb\0"
    ".eer\0"
    ".eg\0"
    ".eld\0"
    ".elem\0"
    ".enam\0"
    ".eng\0"
    ".ens\0"
    ".equit\0"
    ".erri\0"
    ".es\0"
    ".ethyl\0"
    ".eu\0"
    ".euler\0"
    ".ev\0"
    ".eversib\0"
    ".eye\0"
    ".fes\0"
    ".former\0"
    ".ga\0"
    ".gasom\0"
    ".ge\0"
    ".gent\0"
    ".geog\0"
    ".geome\0"
    ".geot\0"
    ".gia\0"
    ".gib\0"
    ".gor\0"
    ".handi\0"
    ".hank\0"
    ".he\0"
    ".hemo\0"
    ".hepa\0"
    ".heroe\0"
    ".heroi\0"
    ".hes\0"
    ".het\0"
    ".hib\0"
    ".hier\0"
    ".honey\0"
    ".hono\0"
    ".hov\0"
    ".idl\0"
    ".idol\0"
    ".imm\0"
    ".impin\0"
    ".in\0"
    ".inci\0"
    ".ine\0"
    ".ink\0"
    ".ins\0"
    ".inut\0"
    ".irr\0"
    ".isi\0"
    ".jur\0"
    ".kilni\0"
    ".korte\0"
    ".lacy\0"
    ".lam\0"
    ".later\0"
    ".lath\0"
    ".le\0"
    ".lege\0"
    ".leices\0"
    ".len\0"
    ".lep\0"
    ".lev\0"
    ".lig\0"
    ".liga\0"
    ".lin\0"
    ".lio\0"
    ".lit\0"
    ".maga\0"
    ".malo\0"
    ".mana\0"
    ".marti\0"
    ".me\0"
    ".megal\0"
    ".merc\0"
    ".metala\0"
    ".meter\0"
    ".mimic\0"
    ".mis\0"
    ".misers\0"
    ".misti\0"
    ".mone\0"
    ".moro\0"
    ".muta\0"
    ".mutab\0"
    ".neof\0"
    ".nic\0"
    ".noeth\0"
    ".nonem\0"
    ".od\0"
    ".odd\0"
    ".ofte\0"
    ".orato\0"
    ".orc\0"
    ".ord\0"
    ".ort\0"
    ".os\0"
    ".ostl\0"
    ".oth\0"
    ".out\0"
    ".pedal\0"
    ".pete\0"
    ".petit\0"
    ".pie\0"
    ".pion\0"
    ".pit\0"
    ".polys\0"
    ".postam\0"
    ".pream\0"
    ".prem\0"
    ".rac\0"
    ".rant\0"
    ".rationa\0"
    ".raveno\0"
    ".ree\0"
    ".reec\0"
    ".remit\0"
    ".res\0"
    ".restat\0"
    ".rig\0"
    ".ritu\0"
    ".roq\0"
    ".rost\0"
    ".rowd\0"
    ".rud\0"
    ".scie\0"
    ".self\0"
    ".sell\0"
    ".semi\0"
    ".semic\0"
    ".semid\0"
    ".semip\0"
    ".semir\0"
    ".semis\0"
    ".semiv\0"
    ".sen\0"
    ".serie\0"
    ".sh\0"
    ".si\0"
    ".sing\0"
    ".sphin\0"
    ".spino\0"
    ".st\0"
    ".stabl\0"
    ".sy\0"
    ".ta\0"
    ".tapestr\0"
    ".te\0"
    ".telegr\0"
    ".tenan\0"
    ".th\0"
    ".ti\0"
    ".til\0"
    ".timo\0"
    ".ting\0"
    ".tink\0"
    ".tona\0"
    ".top\0"
    ".topi\0"
    ".topog\0"
    ".toq\0"
    ".tous\0"
    ".tribut\0"
    ".una\0"
    ".unatt\0"
    ".unce\0"
    ".under\0"
    ".une\0"
    ".unerr\0"
    ".unk\0"
    ".uno\0"
    ".unu\0"
    ".up\0"
    ".ure\0"
    ".usa\0"
    ".vende\0"
    ".vera\0"
    ".vicar\0"
    ".webl\0"
    ".wili\0"
    ".ye\0"
    "ab.\0"
    "abal\0"
    "aban\0"
    "abe\0"
    "aberd\0"
    "abia\0"
    "abitab\0"
    "ablat\0"
    "abolic\0"
    "aboliz\0"
    "abr\0"
    "abrog\0"
    "abul\0"
    "acabl\0"
    "acar\0"
    "acard\0"
    "acaro\0"
    "aceou\0"
    "acer\0"
    "achet\0"
    "aci\0"
    "acie\0"
    "acin\0"
    "acio\0"
    "acrob\0"
    "actif\0"
    "acul\0"
    "acum\0"
    "ad\0"
    "addin\0"
    "ader.\0"
    "adi\0"
    "adia\0"
    "adica\0"
    "adier\0"
    "adio\0"
    "adit\0"
    "adiu\0"
    "adle\0"
    "adow\0"
    "adran\0"
    "adsu\0"
    "adu\0"
    "aduc\0"
    "adum\0"
    "aer\0"
    "aerie\0"
    "af\0"
    "aff\0"
    "affish\0"
    "agab\0"
    "agan\0"
    "agell\0"
    "ageo\0"
    "ageu\0"
    "agi\0"
    "agl\0"
    "agn\0"
    "ago\0"
    "agog\0"
    "agoni\0"
    "aguer\0"
    "agul\0"
    "agy\0"
    "aha\0"
    "ahe\0"
    "ahl\0"
    "aho\0"
    "ai\0"
    "aia\0"
    "aic.\0"
    "aily\0"
    "ain\0"
    "ainin\0"
    "aino\0"
    "aiten\0"
    "aj\0"
    "aken\0"
    "alab\0"
    "alad\0"
    "alar\0"
    "aldi\0"
    "ale\0"
    "alend\0"
    "alenti\0"
    "aleo\0"
    "ali\0"
    "alia.\0"
    "alie\0"
    "allev\0"
    "allic\0"
    "alm\0"
    "alog.\0"
    "aly.\0"
    "alys\0"
    "alyst\0"
    "alyt\0"
    "alyz\0"
    "ama\0"
    "amab\0"
    "amag\0"
    "amara\0"
    "amasc\0"
    "amatis\0"
    "amato\0"
    "amentab\0"
    "amera\0"
    "amic\0"
    "amif\0"
    "amily\0"
    "amin\0"
    "amino\0"
    "amo\0"
    "amon\0"
    "amori\0"
    "ampen\0"
    "an\0"
    "anage\0"
    "analy\0"
    "analys\0"
    "anar\0"
    "anarc\0"
    "anari\0"
    "anati\0"
    "and\0"
    "andes\0"
    "andis\0"
    "andl\0"
    "andow\0"
    "anee\0"
    "anen\0"
    "anest.\0"
    "aneu\0"
    "ang\0"
    "angie\0"
    "angl\0"
    "anic\0"
    "anies\0"
    "anif\0"
    "anime\0"
    "animi\0"
    "anine\0"
    "anio\0"
    "anip\0"
    "anish\0"
    "anit\0"
    "aniu\0"
    "ankli\0"
    "anniz\0"
    "ano\0"
    "anoac\0"
    "anot\0"
    "anoth\0"
    "ansa\0"
    "ansco\0"
    "ansgr\0"
    "ansn\0"
    "ansp\0"
    "anspo\0"
    "anst\0"
    "ansur\0"
    "ansv\0"
    "antal\0"
    "antid\0"
    "antie\0"
    "antin\0"
    "antire\0"
    "anto\0"
    "antr\0"
    "antw\0"
    "anua\0"
    "anul\0"
    "anur\0"
    "ao\0"
    "apar\0"
    "apat\0"
    "apeable\0"
    "apero\0"
    "apher\0"
    "aphi\0"
    "apilla\0"
    "apillar\0"
    "apin\0"
    "apita\0"
    "apitu\0"
    "apl\0"
    "apoc\0"
    "apola\0"
    "apori\0"
    "apost\0"
    "apses\0"
    "apu\0"
    "aque\0"
    "ar\0"
    "aract\0"
    "arade\0"
    "aradis\0"
    "aral\0"
    "aramete\0"
    "arang\0"
    "arap\0"
    "arat\0"
    "aratio\0"
    "arativ\0"
    "arau\0"
    "arav\0"
    "araw\0"
    "arbal\0"
    "archan\0"
    "archet\0"
    "ardine\0"
    "ardr\0"
    "areas\0"
    "aree\0"
    "arent\0"
    "aress\0"
    "arfi\0"
    "arfl\0"
    "ari\0"
    "arial\0"
    "arian\0"
    "ariet\0"
    "arim\0"
    "arinat\0"
    "ario\0"
    "ariz\0"
    "armi\0"
    "arod\0"
    "aroni\0"
    "aroo\0"
    "arp\0"
    "arq\0"
    "arrange\0"
    "arre\0"
    "arsa\0"
    "arsh\0"
    "as.\0"
    "asab\0"
    "asant\0"
    "ashi\0"
    "asia.\0"
    "asib\0"
    "asic\0"
    "asit\0"
    "aski\0"
    "asl\0"
    "asoc\0"
    "asph\0"
    "assh\0"
    "asten\0"
    "astr\0"
    "asura\0"
    "asymptot\0"
    "ata\0"
    "atabl\0"
    "atac\0"
    "atalo\0"
    "atap\0"
    "atec\0"
    "atech\0"
    "atego\0"
    "aten.\0"
    "atera\0"
    "atern\0"
    "aterna\0"
    "atest\0"
    "atev\0"
    "ath\0"
    "athem\0"
    "athen\0"
    "atheros\0"
    "atho\0"
    "athom\0"
    "ati.\0"
    "atia\0"
    "atib\0"
    "atic\0"
    "atif\0"
    "ationar\0"
    "atitu\0"
    "atog\0"
    "atom\0"
    "atomiz\0"
    "atop\0"
    "atos\0"
    "atr\0"
    "atrop\0"
    "atsk\0"
    "attag\0"
    "atte\0"
    "attes.\0"
    "atth\0"
    "atu\0"
    "atua\0"
    "atue\0"
    "atul\0"
    "atura\0"
    "aty\0"
    "aub\0"
    "augh\0"
    "aughtl\0"
    "augu\0"
    "aul\0"
    "aulif\0"
    "aund\0"
    "aur\0"
    "ausib\0"
    "auten\0"
    "auth\0"
    "ava\0"
    "avag\0"
    "avan\0"
    "aveno\0"
    "avera\0"
    "avern\0"
    "avery\0"
    "avi\0"
    "avier\0"
    "avig\0"
    "aviou\0"
    "avoc\0"
    "avor\0"
    "away\0"
    "awi\0"
    "awly\0"
    "aws\0"
    "axic\0"
    "axid\0"
    "ayal\0"
    "aye\0"
    "ays\0"
    "azier\0"
    "azzi\0"
    "ba.\0"
    "backer.\0"
    "badger\0"
    "bage\0"
    "bala\0"
    "bandag\0"
    "bane\0"
    "bani\0"
    "barbi\0"
    "baria\0"
    "baronie\0"
    "bassi\0"
    "bat\0"
    "bathy\0"
    "baz\0"
    "bb\0"
    "bbe\0"
    "bber\0"
    "bbina\0"
    "bbit\0"
    "bd\0"
    "be.\0"
    "beak\0"
    "beat\0"
    "bed\0"
    "beda\0"
    "bede\0"
    "bedi\0"
    "begi\0"
    "begu\0"
    "bel\0"
    "beli\0"
    "belo\0"
    "bem\0"
    "benig\0"
    "benu\0"
    "bes\0"
    "besp\0"
    "bestr\0"
    "bet\0"
    "betiz\0"
    "betr\0"
    "betw\0"
    "bevie\0"
    "bew\0"
    "beyo\0"
    "bf\0"
    "bh\0"
    "bib\0"
    "bid\0"
    "bidif\0"
    "bie\0"
    "bien\0"
    "bier\0"
    "bif\0"
    "bil\0"
    "biliz\0"
    "billab\0"
    "binar\0"
    "bind\0"
    "binet\0"
    "biogr\0"
    "biom\0"
    "biorb\0"
    "biorh\0"
    "biou\0"
    "bit\0"
    "bitio\0"
    "bitive\0"
    "bitr\0"
    "bitua\0"
    "bitz\0"
    "bj\0"
    "bk\0"
    "bl\0"
    "bland\0"
    "blath\0"
    "ble.\0"
    "blen\0"
    "blesp\0"
    "blind\0"
    "blis\0"
    "blo\0"
    "blond\0"
    "blunt\0"
    "bm\0"
    "bn\0"
    "bneg\0"
    "bod\0"
    "bodi\0"
    "boe\0"
    "bolic\0"
    "bombi\0"
    "bona\0"
    "bonat\0"
    "boo\0"
    "bor.\0"
    "bora\0"
    "bord\0"
    "bore\0"
    "bori\0"
    "borno\0"
    "bos\0"
    "bota\0"
    "both\0"
    "boto\0"
    "botul\0"
    "bound\0"
    "bp\0"
    "brit\0"
    "broth\0"
    "brusq\0"
    "bs\0"
    "bsor\0"
    "bt\0"
    "btl\0"
    "bto\0"
    "btr\0"
    "buffer\0"
    "buga\0"
    "buli\0"
    "bumi\0"
    "bun\0"
    "bunti\0"
    "bure\0"
    "busie\0"
    "busier\0"
    "busies\0"
    "busse\0"
    "bussing\0"
    "bust\0"
    "buta\0"
    "buted.\0"
    "butio\0"
    "buto\0"
    "butted\0"
    "bv\0"
    "bw\0"
    "by.\0"
    "bys\0"
    "ca\0"
    "cabin\0"
    "cabl\0"
    "cach\0"
    "cadem\0"
    "caden\0"
    "cag\0"
    "cah\0"
    "calat\0"
    "calla\0"
    "callin\0"
    "calo\0"
    "cand\0"
    "cane\0"
    "canic\0"
    "canis\0"
    "caniz\0"
    "canty\0"
    "cany\0"
    "caper\0"
    "carom\0"
    "caster\0"
    "castig\0"
    "casy\0"
    "catas\0"
    "cath\0"
    "cativ\0"
    "caval\0"
    "cc\0"
    "ccha\0"
    "ccia\0"
    "ccompa\0"
    "ccon\0"
    "ccout\0"
    "ce.\0"
    "ced.\0"
    "ceden\0"
    "cei\0"
    "cel.\0"
    "cell\0"
    "cen\0"
    "cenc\0"
    "cene\0"
    "ceni\0"
    "cent\0"
    "cep\0"
    "ceram\0"
    "cesa\0"
    "cessi\0"
    "cessib\0"
    "cest\0"
    "cet\0"
    "ceta\0"
    "cew\0"
    "ch\0"
    "ch.\0"
    "chab\0"
    "chanic\0"
    "chanis\0"
    "che\0"
    "cheap\0"
    "ched\0"
    "chelo\0"
    "chemi\0"
    "chene\0"
    "cher.\0"
    "chers\0"
    "chievo\0"
    "chin\0"
    "chine.\0"
    "chiness\0"
    "chini\0"
    "chio\0"
    "chit\0"
    "chiz\0"
    "cho\0"
    "chs.\0"
    "chshu\0"
    "chti\0"
    "ci\0"
    "cia\0"
    "ciab\0"
    "ciar\0"
    "cic\0"
    "cier\0"
    "cific.\0"
    "cigar\0"
    "cii\0"
    "cila\0"
    "cili\0"
    "cim\0"
    "cin\0"
    "cina\0"
    "cinat\0"
    "cinem\0"
    "cing\0"
    "cing.\0"
    "cino\0"
    "cinq\0"
    "cion\0"
    "cipe\0"
    "ciph\0"
    "cipic\0"
    "cista\0"
    "cisti\0"
    "cit\0"
    "citiz\0"
    "ciz\0"
    "ck\0"
    "cki\0"
    "cl\0"
    "clar\0"
    "claratio\0"
    "clare\0"
    "clear\0"
    "clem\0"
    "clic\0"
    "clim\0"
    "cly\0"
    "cn\0"
    "co\0"
    "coag\0"
    "coe\0"
    "cog\0"
    "cogr\0"
    "coi\0"
    "coinc\0"
    "coli\0"
    "colo\0"
    "color\0"
    "comer\0"
    "cona\0"
    "cone\0"
    "cong\0"
    "cont\0"
    "copa\0"
    "cophon\0"
    "copic\0"
    "copl\0"
    "corb\0"
    "coron\0"
    "cose\0"
    "cousti\0"
    "cov\0"
    "cove\0"
    "cowa\0"
    "coze\0"
    "cozi\0"
    "cq\0"
    "crast\0"
    "crat.\0"
    "cratic\0"
    "creat\0"
    "cred\0"
    "creta\0"
    "crev\0"
    "cri\0"
    "crif\0"
    "crin\0"
    "cris\0"
    "criti\0"
    "critie\0"
    "crocod\0"
    "croeco\0"
    "cropl\0"
    "cropo\0"
    "crose\0"
    "crud\0"
    "cs\0"
    "ct\0"
    "ctab\0"
    "ctang\0"
    "ctant\0"
    "cte\0"
    "cter\0"
    "cticu\0"
    "ctimi\0"
    "ctromec\0"
    "ctur\0"
    "ctw\0"
    "cud\0"
    "cuf\0"
    "cui\0"
    "cuity\0"
    "culi\0"
    "cultis\0"
    "cultu\0"
    "cuma\0"
    "cume\0"
    "cumi\0"
    "cun\0"
    "cupi\0"
    "cupy\0"
    "curab\0"
    "curance\0"
    "curia\0"
    "cus\0"
    "cussi\0"
    "cut\0"
    "cutie\0"
    "cutiv\0"
    "cutr\0"
    "cy\0"
    "cze\0"
    "da\0"
    "da.\0"
    "dab\0"
    "dach\0"
    "daf\0"
    "dag\0"
    "dalone\0"
    "dam\0"
    "dang\0"
    "dard\0"
    "dark\0"
    "dary\0"
    "dat\0"
    "datab\0"
    "dativ\0"
    "dato\0"
    "dav\0"
    "dave\0"
    "day\0"
    "db\0"
    "dc\0"
    "dd\0"
    "ddab\0"
    "ddib\0"
    "de.\0"
    "deaf\0"
    "deals.\0"
    "debit\0"
    "debon\0"
    "decan\0"
    "decil\0"
    "declar\0"
    "declina\0"
    "decom\0"
    "ded\0"
    "dee.\0"
    "definiti\0"
    "deif\0"
    "delie\0"
    "deliq\0"
    "delo\0"
    "dem\0"
    "dem.\0"
    "demic\0"
    "demic.\0"
    "demil\0"
    "demons\0"
    "demor\0"
    "demos\0"
    "den\0"
    "denar\0"
    "deno\0"
    "dentif\0"
    "denu\0"
    "dep\0"
    "depa\0"
    "depi\0"
    "depu\0"
    "deq\0"
    "derh\0"
    "derm\0"
    "derniz\0"
    "ders\0"
    "des\0"
    "des.\0"
    "desc\0"
    "desic\0"
    "deso\0"
    "desti\0"
    "destr\0"
    "desu\0"
    "det\0"
    "detic\0"
    "deto\0"
    "dev\0"
    "devil\0"
    "dey\0"
    "df\0"
    "dga\0"
    "dget\0"
    "dgi\0"
    "dgy\0"
    "dh\0"
    "di.\0"
    "dia\0"
    "diab\0"
    "dicaid\0"
    "dicam\0"
    "dice\0"
    "dict\0"
    "did\0"
    "dien\0"
    "dif\0"
    "diffra\0"
    "dige\0"
    "dilato\0"
    "dimethy\0"
    "din\0"
    "dina\0"
    "dine.\0"
    "dini\0"
    "diniz\0"
    "dio\0"
    "diog\0"
    "dipl\0"
    "dir\0"
    "dire\0"
    "diren\0"
    "direr\0"
    "dirti\0"
    "dis\0"
    "disi\0"
    "dist\0"
    "diti\0"
    "div\0"
    "dj\0"
    "dk\0"
    "dla\0"
    "dle.\0"
    "dlead\0"
    "dled\0"
    "dles.\0"
    "dless\0"
    "dlie\0"
    "dlo\0"
    "dlu\0"
    "dly\0"
    "dm\0"
    "dn\0"
    "do\0"
    "do.\0"
    "dode\0"
    "doe\0"
    "dof\0"
    "dog\0"
    "dola\0"
    "doli\0"
    "dolor\0"
    "domiz\0"
    "donat\0"
    "doni\0"
    "dood\0"
    "dopp\0"
    "dor\0"
    "dos\0"
    "dout\0"
    "dov\0"
    "doword\0"
    "dox\0"
    "dp\0"
    "dr\0"
    "dragon\0"
    "drai\0"
    "dre\0"
    "drear\0"
    "dren\0"
    "drenal\0"
    "drib\0"
    "drifta\0"
    "dril\0"
    "dripleg\0"
    "dromed\0"
    "drop\0"
    "drow\0"
    "drupli\0"
    "dry\0"
    "ds\0"
    "dsp\0"
    "dsw\0"
    "dsy\0"
    "dtab\0"
    "dth\0"
    "du\0"
    "dua\0"
    "dual.\0"
    "duc\0"
    "duca\0"
    "ducer\0"
    "duct.\0"
    "ducts\0"
    "duel\0"
    "dug\0"
    "dule\0"
    "dumbe\0"
    "dun\0"
    "duopol\0"
    "dup\0"
    "dupe\0"
    "dv\0"
    "dw\0"
    "dy\0"
    "dyn\0"
    "dyse\0"
    "dysp\0"
    "eab\0"
    "eact\0"
    "ead\0"
    "eadie\0"
    "eage\0"
    "eager\0"
    "eal\0"
    "ealer\0"
    "ealou\0"
    "eamer\0"
    "eand\0"
    "eanies\0"
    "eara\0"
    "earc\0"
    "eares\0"
    "earic\0"
    "earil\0"
    "eark\0"
    "eart\0"
    "earte\0"
    "easp\0"
    "eass\0"
    "east\0"
    "eat\0"
    "eaten\0"
    "eathi\0"
    "eatif\0"
    "eatu\0"
    "eav\0"
    "eaven\0"
    "eavi\0"
    "eavo\0"
    "eb\0"
    "ebel.\0"
    "ebels\0"
    "eben\0"
    "ebit\0"
    "ebr\0"
    "ecad\0"
    "ecanc\0"
    "ecca\0"
    "ece\0"
    "ecessa\0"
    "echas\0"
    "eci\0"
    "ecib\0"
    "ecificat\0"
    "ecifie\0"
    "ecify\0"
    "ecim\0"
    "ecit\0"
    "ecite\0"
    "eclam\0"
    "eclus\0"
    "ecol\0"
    "ecomm\0"
    "ecompe\0"
    "econc\0"
    "ecor\0"
    "ecora\0"
    "ecoro\0"
    "ecr\0"
    "ecrem\0"
    "ectan\0"
    "ecte\0"
    "ecu\0"
    "ecul\0"
    "ecula\0"
    "eda\0"
    "edd\0"
    "eder\0"
    "edes\0"
    "edgl\0"
    "edi\0"
    "edia\0"
    "edib\0"
    "edica\0"
    "edim\0"
    "edit\0"
    "ediz\0"
    "edo\0"
    "edol\0"
    "edon\0"
    "edri\0"
    "edul\0"
    "eduling\0"
    "edulo\0"
    "eec\0"
    "eedi\0"
    "eef\0"
    "eeli\0"
    "eely\0"
    "eem\0"
    "eena\0"
    "eep\0"
    "ees\0"
    "eest\0"
    "eety\0"
    "eex\0"
    "ef\0"
    "efere\0"
    "eff\0"
    "efic\0"
    "efici\0"
    "efil\0"
    "efine\0"
    "efinite\0"
    "efit\0"
    "efores\0"
    "efuse.\0"
    "egal\0"
    "eger\0"
    "egib\0"
    "egic\0"
    "eging\0"
    "egit\0"
    "egn\0"
    "ego.\0"
    "egos\0"
    "egul\0"
    "egur\0"
    "egy\0"
    "eh\0"
    "eher\0"
    "ei\0"
    "eic\0"
    "eid\0"
    "eig\0"
    "eigl\0"
    "eimb\0"
    "einf\0"
    "eing\0"
    "einst\0"
    "eird\0"
    "eite\0"
    "eith\0"
    "eity\0"
    "ej\0"
    "ejud\0"
    "ejudi\0"
    "ekin\0"
    "ekla\0"
    "ela\0"
    "ela.\0"
    "elac\0"
    "eland\0"
    "elativ\0"
    "elaw\0"
    "elaxa\0"
    "elea\0"
    "elebra\0"
    "elec\0"
    "eled\0"
    "elega\0"
    "elen\0"
    "eler\0"
    "eles\0"
    "elf\0"
    "eli\0"
    "elibe\0"
    "elic.\0"
    "elica\0"
    "elier\0"
    "eligib\0"
    "elim\0"
    "eling\0"
    "elio\0"
    "elis\0"
    "elish\0"
    "elitis\0"
    "eliv\0"
    "ella\0"
    "ellab\0"
    "ello\0"
    "eloa\0"
    "eloc\0"
    "elog\0"
    "elop.\0"
    "elsh\0"
    "elta\0"
    "elud\0"
    "elug\0"
    "emac\0"
    "emag\0"
    "eman\0"
    "emana\0"
    "emb\0"
    "eme\0"
    "emel\0"
    "emet\0"
    "emica\0"
    "emie\0"
    "emigra\0"
    "emin\0"
    "emine\0"
    "emini\0"
    "emis\0"
    "emish\0"
    "emiss\0"
    "emiz\0"
    "emniz\0"
    "emog\0"
    "emonio\0"
    "empi\0"
    "emul\0"
    "emula\0"
    "emun\0"
    "emy\0"
    "enamo\0"
    "enant\0"
    "encher\0"
    "endic\0"
    "endix\0"
    "enea\0"
    "enee\0"
    "enem\0"
    "enero\0"
    "enesi\0"
    "enest\0"
    "enetr\0"
    "enew\0"
    "enics\0"
    "enie\0"
    "enil\0"
    "enio\0"
    "enish\0"
    "enit\0"
    "eniu\0"
    "eniz\0"
    "enn\0"
    "eno\0"
    "enog\0"
    "enos\0"
    "enov\0"
    "ensw\0"
    "entage\0"
    "enthes\0"
    "enua\0"
    "enuf\0"
    "eny.\0"
    "enz\0"
    "eof\0"
    "eog\0"
    "eograp\0"
    "eoi\0"
    "eol\0"
    "eopar\0"
    "eor\0"
    "eore\0"
    "eorol\0"
    "eos\0"
    "eot\0"
    "eoto\0"
    "eout\0"
    "eow\0"
    "epa\0"
    "epai\0"
    "epanc\0"
    "epel\0"
    "epent\0"
    "epetitio\0"
    "ephe\0"
    "epineph\0"
    "epli\0"
    "epo\0"
    "eprec\0"
    "epreca\0"
    "epred\0"
    "epreh\0"
    "epro\0"
    "eprob\0"
    "epsh\0"
    "eptib\0"
    "eput\0"
    "eputa\0"
    "eq\0"
    "equil\0"
    "equis\0"
    "era\0"
    "erab\0"
    "erand\0"
    "erar\0"
    "erati.\0"
    "erb\0"
    "erbl\0"
    "erch\0"
    "erche\0"
    "ere.\0"
    "ereal\0"
    "ereco\0"
    "erein\0"
    "erel.\0"
    "eremo\0"
    "erena\0"
    "erence\0"
    "erene\0"
    "erent\0"
    "ereq\0"
    "eress\0"
    "erest\0"
    "eret\0"
    "erh\0"
    "eri\0"
    "eria\0"
    "erian.\0"
    "erick\0"
    "erien\0"
    "erier\0"
    "erine\0"
    "erio\0"
    "erit\0"
    "eriu\0"
    "eriv\0"
    "eriva\0"
    "erm\0"
    "ernis\0"
    "ernit\0"
    "erniz\0"
    "erno\0"
    "ero\0"
    "erob\0"
    "eroc\0"
    "eror\0"
    "erou\0"
    "ers\0"
    "erset\0"
    "erter\0"
    "ertl\0"
    "ertw\0"
    "eru\0"
    "erut\0"
    "erwau\0"
    "esa\0"
    "esage.\0"
    "esages\0"
    "esc\0"
    "esca\0"
    "escan\0"
    "escr\0"
    "escu\0"
    "ese\0"
    "esec\0"
    "esecr\0"
    "esenc\0"
    "esert.\0"
    "eserts\0"
    "eserva\0"
    "esh\0"
    "esha\0"
    "eshen\0"
    "esi\0"
    "esic\0"
    "esid\0"
    "esiden\0"
    "esigna\0"
    "esim\0"
    "esin\0"
    "esiste\0"
    "esiu\0"
    "eskin\0"
    "esmi\0"
    "esol\0"
    "esolu\0"
    "eson\0"
    "esona\0"
    "esp\0"
    "espaci\0"
    "esper\0"
    "espira\0"
    "espre\0"
    "ess\0"
    "essib\0"
    "estan\0"
    "estig\0"
    "estim\0"
    "esto\0"
    "eston\0"
    "estr\0"
    "estro\0"
    "estruc\0"
    "esur\0"
    "esurr\0"
    "esw\0"
    "etab\0"
    "etend\0"
    "eteo\0"
    "ethod\0"
    "ethylene\0"
    "etic\0"
    "etide\0"
    "etin\0"
    "etino\0"
    "etir\0"
    "etitio\0"
    "etitiv\0"
    "etn\0"
    "etona\0"
    "etra\0"
    "etre\0"
    "etric\0"
    "etrif\0"
    "etrog\0"
    "etros\0"
    "etua\0"
    "etym\0"
    "etz\0"
    "eu\0"
    "euclid\0"
    "eun\0"
    "eup\0"
    "euro\0"
    "eus\0"
    "eute\0"
    "eutil\0"
    "eutr\0"
    "evap\0"
    "evas\0"
    "evast\0"
    "evea\0"
    "evell\0"
    "evelo\0"
    "eveng\0"
    "eveni\0"
    "ever\0"
    "everb\0"
    "evi\0"
    "evid\0"
    "evil\0"
    "evin\0"
    "eviv\0"
    "evoc\0"
    "evu\0"
    "ewa\0"
    "ewag\0"
    "ewee\0"
    "ewh\0"
    "ewil\0"
    "ewing\0"
    "ewit\0"
    "exp\0"
    "eyc\0"
    "eye.\0"
    "eys\0"
    "fa\0"
    "fabl\0"
    "fabr\0"
    "face\0"
    "fag\0"
    "fain\0"
    "falle\0"
    "fama\0"
    "famis\0"
    "far\0"
    "farth\0"
    "fata\0"
    "fathe\0"
    "fato\0"
    "fault\0"
    "fb\0"
    "fd\0"
    "fe.\0"
    "feas\0"
    "feath\0"
    "feb\0"
    "februa\0"
    "feca\0"
    "fect\0"
    "fed\0"
    "feli\0"
    "femo\0"
    "fend\0"
    "fende\0"
    "fer\0"
    "fermio\0"
    "ferr\0"
    "fev\0"
    "ff\0"
    "ffes\0"
    "ffie\0"
    "ffin.\0"
    "ffis\0"
    "ffly\0"
    "ffy\0"
    "fh\0"
    "fi\0"
    "fia\0"
    "fic.\0"
    "fical\0"
    "fican\0"
    "ficate\0"
    "ficen\0"
    "ficer\0"
    "fich\0"
    "fici\0"
    "ficia\0"
    "ficie\0"
    "fics\0"
    "ficu\0"
    "fidel\0"
    "fight\0"
    "fili\0"
    "fillin\0"
    "fily\0"
    "fin\0"
    "fina\0"
    "find\0"
    "fine\0"
    "fing\0"
    "finn\0"
    "fisti\0"
    "fitted.\0"
    "fl\0"
    "flagel\0"
    "fless\0"
    "flin\0"
    "flore\0"
    "flower.\0"
    "fluor\0"
    "fly\0"
    "fm\0"
    "fn\0"
    "fo\0"
    "fon\0"
    "fonde\0"
    "font\0"
    "for\0"
    "forat\0"
    "foray\0"
    "foret\0"
    "fori\0"
    "forta\0"
    "fos\0"
    "fp\0"
    "frat\0"
    "frea\0"
    "fresc\0"
    "fri\0"
    "fril\0"
    "frol\0"
    "fs\0"
    "ft\0"
    "fto\0"
    "fty\0"
    "fu\0"
    "fuel\0"
    "fug\0"
    "fumin\0"
    "fune\0"
    "furi\0"
    "fusi\0"
    "fuss\0"
    "futa\0"
    "fy\0"
    "ga\0"
    "gaf\0"
    "gal.\0"
    "gali\0"
    "galo\0"
    "gam\0"
    "gamet\0"
    "gamo\0"
    "ganis\0"
    "ganiz\0"
    "ganiza\0"
    "gano\0"
    "garn\0"
    "gass\0"
    "gath\0"
    "gativ\0"
    "gaz\0"
    "gb\0"
    "gd\0"
    "ge.\0"
    "ged\0"
    "geez\0"
    "gelin\0"
    "gelis\0"
    "geliz\0"
    "gely\0"
    "gen\0"
    "genat\0"
    "gency.\0"
    "geniz\0"
    "geno\0"
    "geny\0"
    "geo\0"
    "geod\0"
    "geom\0"
    "gery\0"
    "gesi\0"
    "geth\0"
    "getic.\0"
    "geto\0"
    "gety\0"
    "gev\0"
    "gg\0"
    "gge\0"
    "gger\0"
    "gglu\0"
    "ggo\0"
    "ghin\0"
    "ghout\0"
    "ghto\0"
    "ghtwe\0"
    "gi.\0"
    "gia\0"
    "giar\0"
    "gic\0"
    "gicia\0"
    "gico\0"
    "gien\0"
    "gies.\0"
    "gil\0"
    "gimen\0"
    "gin.\0"
    "ginge\0"
    "gins\0"
    "gio\0"
    "gir\0"
    "girl\0"
    "gisl\0"
    "giu\0"
    "giv\0"
    "giz\0"
    "gl\0"
    "gla\0"
    "gladi\0"
    "glas\0"
    "gle\0"
    "glead\0"
    "glib\0"
    "glig\0"
    "glish\0"
    "glo\0"
    "globin\0"
    "glor\0"
    "gm\0"
    "gmy\0"
    "gna\0"
    "gna.\0"
    "gnac\0"
    "gnetism\0"
    "gnett\0"
    "gni\0"
    "gnin\0"
    "gnio\0"
    "gno\0"
    "gnomo\0"
    "gnon\0"
    "gnor.\0"
    "gnoresp\0"
    "go\0"
    "go.\0"
    "gob\0"
    "goe\0"
    "gog\0"
    "gois\0"
    "gon\0"
    "gona\0"
    "gondo\0"
    "goni\0"
    "goniza\0"
    "goo\0"
    "goriz\0"
    "gorou\0"
    "gos.\0"
    "gov\0"
    "gp\0"
    "gr\0"
    "grada\0"
    "grai\0"
    "gran\0"
    "graph.\0"
    "grapher\0"
    "grapher.\0"
    "graphic\0"
    "graphy\0"
    "gray\0"
    "gren\0"
    "gress.\0"
    "griev\0"
    "grit\0"
    "gro\0"
    "gruf\0"
    "gs\0"
    "gste\0"
    "gth\0"
    "gua\0"
    "guard\0"
    "gue\0"
    "guit\0"
    "gun\0"
    "gus\0"
    "gut\0"
    "gutan\0"
    "gw\0"
    "gy\0"
    "gyn\0"
    "gyra\0"
    "habl\0"
    "hach\0"
    "haem\0"
    "haet\0"
    "hagu\0"
    "hairs\0"
    "hala\0"
    "halam\0"
    "ham\0"
    "hanci\0"
    "hancy\0"
    "hand.\0"
    "hang\0"
    "hanger\0"
    "hango\0"
    "haniz\0"
    "hank\0"
    "hante\0"
    "haparr\0"
    "hapl\0"
    "hapt\0"
    "haran\0"
    "haras\0"
    "hard\0"
    "harde\0"
    "harle\0"
    "harpen\0"
    "harter\0"
    "hass\0"
    "hatch\0"
    "haun\0"
    "haz\0"
    "haza\0"
    "hb\0"
    "head\0"
    "hear\0"
    "hecan\0"
    "hecat\0"
    "hed\0"
    "hedo\0"
    "heli\0"
    "hellis\0"
    "helly\0"
    "helo\0"
    "hemp\0"
    "hen\0"
    "hena\0"
    "henat\0"
    "heor\0"
    "hep\0"
    "hera\0"
    "herap\0"
    "herba\0"
    "herea\0"
    "hern\0"
    "herou\0"
    "hery\0"
    "hes\0"
    "hesp\0"
    "het\0"
    "heted\0"
    "heu\0"
    "hexa\0"
    "hf\0"
    "hh\0"
    "hian\0"
    "hico\0"
    "high\0"
    "hil\0"
    "himer\0"
    "hina\0"
    "hione\0"
    "hip\0"
    "hipela\0"
    "hirl\0"
    "hiro\0"
    "hirp\0"
    "hirr\0"
    "hisel\0"
    "hiss\0"
    "hitesid\0"
    "hither\0"
    "hiv\0"
    "hk\0"
    "hl\0"
    "hlan\0"
    "hlo\0"
    "hlori\0"
    "hm\0"
    "hmet\0"
    "hn\0"
    "hnauz\0"
    "hodiz\0"
    "hods\0"
    "hog\0"
    "hoge\0"
    "holar\0"
    "hole\0"
    "homa\0"
    "home\0"
    "hona\0"
    "hony\0"
    "hood\0"
    "hoon\0"
    "horat\0"
    "horic.\0"
    "horis\0"
    "horte\0"
    "horu\0"
    "hose\0"
    "hosen\0"
    "hosp\0"
    "hous\0"
    "house\0"
    "hovel\0"
    "hp\0"
    "hr\0"
    "hree\0"
    "hroniz\0"
    "hropo\0"
    "hs\0"
    "hsh\0"
    "htar\0"
    "hten\0"
    "hteou\0"
    "htes\0"
    "hty\0"
    "hug\0"
    "humin\0"
    "hunke\0"
    "hunt\0"
    "hust\0"
    "hut\0"
    "hw\0"
    "hwart\0"
    "hype\0"
    "hyph\0"
    "hypotha\0"
    "hys\0"
    "ia\0"
    "ial\0"
    "iam\0"
    "iamete\0"
    "ian\0"
    "ianc\0"
    "iani\0"
    "iant\0"
    "iape\0"
    "iass\0"
    "iativ\0"
    "iatric\0"
    "iatu\0"
    "ibe\0"
    "ibera\0"
    "ibert\0"
    "ibia\0"
    "ibin\0"
    "ibit.\0"
    "ibite\0"
    "ibl\0"
    "ibli\0"
    "ibo\0"
    "ibr\0"
    "ibri\0"
    "ibun\0"
    "icam\0"
    "icap\0"
    "icar\0"
    "icar.\0"
    "icara\0"
    "icas\0"
    "icay\0"
    "iccu\0"
    "iceo\0"
    "ich\0"
    "ici\0"
    "icid\0"
    "icina\0"
    "icip\0"
    "icipa\0"
    "icly\0"
    "icoc\0"
    "icr\0"
    "icra\0"
    "icry\0"
    "icte\0"
    "ictu\0"
    "ictua\0"
    "icula\0"
    "icum\0"
    "icuo\0"
    "icur\0"
    "id\0"
    "idai\0"
    "idanc\0"
    "idd\0"
    "ideal\0"
    "ides\0"
    "idi\0"
    "idian\0"
    "idiar\0"
    "idie\0"
    "idio\0"
    "idios\0"
    "idiou\0"
    "idit\0"
    "idiu\0"
    "idle\0"
    "idom\0"
    "idow\0"
    "idr\0"
    "idu\0"
    "iduo\0"
    "ie\0"
    "iede\0"
    "iega\0"
    "ield\0"
    "iena\0"
    "iene\0"
    "ienn\0"
    "ienti\0"
    "ier.\0"
    "iesc\0"
    "iest\0"
    "iet\0"
    "if.\0"
    "ifacet\0"
    "ifero\0"
    "iffen\0"
    "iffr\0"
    "ific.\0"
    "ifie\0"
    "ifl\0"
    "ift\0"
    "ig\0"
    "igab\0"
    "igera\0"
    "ighti\0"
    "igi\0"
    "igib\0"
    "igil\0"
    "igin\0"
    "igit\0"
    "igl\0"
    "ignit\0"
    "igniter\0"
    "igo\0"
    "igor\0"
    "igot\0"
    "igre\0"
    "igui\0"
    "igur\0"
    "ih\0"
    "ii\0"
    "ij\0"
    "ijk\0"
    "ik\0"
    "ila\0"
    "ilab\0"
    "ilade\0"
    "ilam\0"
    "ilara\0"
    "ileg\0"
    "iler\0"
    "ilev\0"
    "ilf\0"
    "ili\0"
    "ilia\0"
    "ilib\0"
    "ilio\0"
    "ilist\0"
    "ilit\0"
    "iliz\0"
    "illab\0"
    "iln\0"
    "iloq\0"
    "ilty\0"
    "ilur\0"
    "ilv\0"
    "imag\0"
    "image\0"
    "imary\0"
    "imentar\0"
    "imet\0"
    "imi\0"
    "imida\0"
    "imile\0"
    "imini\0"
    "imit\0"
    "imni\0"
    "imon\0"
    "impeda\0"
    "imu\0"
    "imula\0"
    "in.\0"
    "inau\0"
    "inav\0"
    "incel\0"
    "incer\0"
    "ind\0"
    "indling\0"
    "ine\0"
    "inee\0"
    "inerar\0"
    "iness\0"
    "infras\0"
    "inga\0"
    "inge\0"
    "ingen\0"
    "ingi\0"
    "ingling\0"
    "ingo\0"
    "ingu\0"
    "ini\0"
    "ini.\0"
    "inia\0"
    "inio\0"
    "inis\0"
    "inite.\0"
    "initely.\0"
    "initio\0"
    "inity\0"
    "ink\0"
    "inl\0"
    "inn\0"
    "ino\0"
    "inoc\0"
    "inos\0"
    "inot\0"
    "ins\0"
    "inse\0"
    "insura\0"
    "int.\0"
    "inth\0"
    "inu\0"
    "inus\0"
    "iny\0"
    "io\0"
    "io.\0"
    "ioge\0"
    "iogr\0"
    "iol\0"
    "iom\0"
    "ionat\0"
    "ionery\0"
    "ioni\0"
    "ioph\0"
    "iori\0"
    "ios\0"
    "ioth\0"
    "ioti\0"
    "ioto\0"
    "iour\0"
    "ip\0"
    "ipe\0"
    "iphras\0"
    "ipi\0"
    "ipic\0"
    "ipre\0"
    "ipul\0"
    "iqua\0"
    "iquef\0"
    "iquid\0"
    "iquit\0"
    "ir\0"
    "ira\0"
    "irab\0"
    "irac\0"
    "irde\0"
    "irede\0"
    "iref\0"
    "irel\0"
    "ires\0"
    "irgi\0"
    "iri\0"
    "iride\0"
    "iris\0"
    "iritu\0"
    "iriz\0"
    "irmin\0"
    "irog\0"
    "iron.\0"
    "irrevoc\0"
    "irul\0"
    "is.\0"
    "isag\0"
    "isar\0"
    "isas\0"
    "isc\0"
    "isch\0"
    "ise\0"
    "iser\0"
    "isf\0"
    "ishan\0"
    "ishon\0"
    "ishop\0"
    "isib\0"
    "isid\0"
    "isis\0"
    "isitiv\0"
    "isk\0"
    "islan\0"
    "isms\0"
    "iso\0"
    "isomer\0"
    "isp\0"
    "ispi\0"
    "ispy\0"
    "iss\0"
    "issal\0"
    "issen\0"
    "isses\0"
    "ista.\0"
    "iste\0"
    "isti\0"
    "istly\0"
    "istral\0"
    "isu\0"
    "isus\0"
    "ita.\0"
    "itabi\0"
    "itag\0"
    "itam\0"
    "itan\0"
    "itat\0"
    "ite\0"
    "itera\0"
    "iteri\0"
    "ites\0"
    "itesima\0"
    "ith\0"
    "ithil\0"
    "iti\0"
    "itia\0"
    "itic\0"
    "itica\0"
    "itick\0"
    "itig\0"
    "itill\0"
    "itim\0"
    "itinerar\0"
    "itio\0"
    "itis\0"
    "itism\0"
    "itom\0"
    "iton\0"
    "itram\0"
    "itry\0"
    "itt\0"
    "ituat\0"
    "itud\0"
    "itul\0"
    "itz.\0"
    "iu\0"
    "iv\0"
    "ivell\0"
    "iven.\0"
    "iver.\0"
    "ivers.\0"
    "ivil.\0"
    "ivio\0"
    "ivit\0"
    "ivore\0"
    "ivoro\0"
    "ivot\0"
    "iw\0"
    "ixo\0"
    "iy\0"
    "izar\0"
    "izi\0"
    "izont\0"
    "ja\0"
    "jacq\0"
    "janua\0"
    "jap\0"
    "japanes\0"
    "je\0"
    "jerem\0"
    "jers\0"
    "jestie\0"
    "jesty\0"
    "jew\0"
    "jop\0"
    "judg\0"
    "ka.\0"
    "kab\0"
    "kag\0"
    "kais\0"
    "kal\0"
    "kb\0"
    "ked\0"
    "kee\0"
    "keg\0"
    "keli\0"
    "keling\0"
    "kend\0"
    "ker\0"
    "kes\0"
    "kest.\0"
    "kety\0"
    "kf\0"
    "kh\0"
    "ki\0"
    "ki.\0"
    "kic\0"
    "kill\0"
    "kilo\0"
    "kim\0"
    "kin.\0"
    "kinde\0"
    "kiness\0"
    "kinetic\0"
    "king\0"
    "kip\0"
    "kis\0"
    "kish\0"
    "kk\0"
    "kl\0"
    "kley\0"
    "kly\0"
    "km\0"
    "knes\0"
    "kno\0"
    "kor\0"
    "kosh\0"
    "kou\0"
    "kovian\0"
    "kron\0"
    "ks\0"
    "ksc\0"
    "ksha\0"
    "ksl\0"
    "ksy\0"
    "kt\0"
    "kw\0"
    "labic\0"
    "labo\0"
    "laci\0"
    "lacie\0"
    "lade\0"
    "lady\0"
    "lagn\0"
    "lainess\0"
    "lamo\0"
    "land\0"
    "landl\0"
    "lanet\0"
    "lante\0"
    "larcen\0"
    "larg\0"
    "lari\0"
    "lase\0"
    "latan\0"
    "lateli\0"
    "lativ\0"
    "lav\0"
    "lava\0"
    "lb\0"
    "lbin\0"
    "lc\0"
    "lce\0"
    "lchai\0"
    "lchild\0"
    "lci\0"
    "ld\0"
    "lde\0"
    "ldere\0"
    "lderi\0"
    "ldi\0"
    "ldis\0"
    "ldr\0"
    "ldri\0"
    "lea\0"
    "leader.\0"
    "leasa\0"
    "lebi\0"
    "lectab\0"
    "left\0"
    "leg.\0"
    "legendre\0"
    "legg\0"
    "lemat\0"
    "lematic\0"
    "len.\0"
    "lenc\0"
    "lene.\0"
    "lenoid\0"
    "lent\0"
    "leph\0"
    "lepr\0"
    "lerab\0"
    "lere\0"
    "lerg\0"
    "leri\0"
    "lero\0"
    "les\0"
    "lesco\0"
    "lesq\0"
    "less\0"
    "less.\0"
    "leva\0"
    "lever.\0"
    "levera\0"
    "levers\0"
    "ley\0"
    "leye\0"
    "lf\0"
    "lfr\0"
    "lg\0"
    "lga\0"
    "lgar\0"
    "lges\0"
    "lgo\0"
    "lh\0"
    "liag\0"
    "liam\0"
    "liariz\0"
    "lias\0"
    "liato\0"
    "libi\0"
    "licio\0"
    "licor\0"
    "lics\0"
    "lict.\0"
    "licu\0"
    "licy\0"
    "lida\0"
    "lider\0"
    "lidi\0"
    "lifer\0"
    "liff\0"
    "lifl\0"
    "ligate\0"
    "ligh\0"
    "ligra\0"
    "lik\0"
    "lil\0"
    "limbl\0"
    "limi\0"
    "limo\0"
    "limp\0"
    "lina\0"
    "line\0"
    "linea\0"
    "lini\0"
    "linker\0"
    "liog\0"
    "liq\0"
    "lisp\0"
    "lit\0"
    "lit.\0"
    "lithog\0"
    "litica\0"
    "litics\0"
    "liver\0"
    "liz\0"
    "lj\0"
    "lka\0"
    "lkal\0"
    "lkat\0"
    "ll\0"
    "llaw\0"
    "lle\0"
    "llea\0"
    "llec\0"
    "lleg\0"
    "llel\0"
    "llen\0"
    "llet\0"
    "llfl\0"
    "lli\0"
    "llin\0"
    "llina\0"
    "llish\0"
    "llo\0"
    "lloqui\0"
    "llout\0"
    "llow\0"
    "lm\0"
    "lmet\0"
    "lming\0"
    "lmod\0"
    "lmon\0"
    "lmonell\0"
    "ln\0"
    "lo.\0"
    "loaded.\0"
    "loader.\0"
    "lobal\0"
    "loboto\0"
    "loci\0"
    "lof\0"
    "loges.\0"
    "logic\0"
    "logo\0"
    "logu\0"
    "lomer\0"
    "long\0"
    "loni\0"
    "loniz\0"
    "lood\0"
    "lope.\0"
    "lopi\0"
    "lopm\0"
    "lora\0"
    "lorato\0"
    "lorie\0"
    "lorou\0"
    "los.\0"
    "loset\0"
    "losophiz\0"
    "losophy\0"
    "lost\0"
    "lota\0"
    "lound\0"
    "lout\0"
    "lov\0"
    "lp\0"
    "lpab\0"
    "lpha\0"
    "lphi\0"
    "lping\0"
    "lpit\0"
    "lpl\0"
    "lpr\0"
    "lr\0"
    "ls\0"
    "lsc\0"
    "lse\0"
    "lsie\0"
    "lt\0"
    "ltag\0"
    "ltane\0"
    "lte\0"
    "ltea\0"
    "lten\0"
    "ltera\0"
    "lthi\0"
    "lthily\0"
    "lties.\0"
    "ltis\0"
    "ltr\0"
    "ltu\0"
    "ltura\0"
    "lua\0"
    "lubr\0"
    "luch\0"
    "luci\0"
    "luen\0"
    "luep\0"
    "luf\0"
    "luid\0"
    "luma\0"
    "lumbia.\0"
    "lumi\0"
    "lumn.\0"
    "lumnia\0"
    "lunker\0"
    "luo\0"
    "luor\0"
    "lup\0"
    "luss\0"
    "luste\0"
    "lut\0"
    "lven\0"
    "lvet\0"
    "lw\0"
    "ly\0"
    "lya\0"
    "lyb\0"
    "lygami\0"
    "lyme\0"
    "lyno\0"
    "lys\0"
    "lyse\0"
    "lystyr\0"
    "ma\0"
    "mab\0"
    "maca\0"
    "machine\0"
    "macl\0"
    "magin\0"
    "magn\0"
    "mah\0"
    "maid\0"
    "malap\0"
    "mald\0"
    "malig\0"
    "malin\0"
    "malli\0"
    "malty\0"
    "man.\0"
    "mania\0"
    "manis\0"
    "maniz\0"
    "manusc\0"
    "map\0"
    "maphro\0"
    "margin\0"
    "marine.\0"
    "mariz\0"
    "marly\0"
    "marv\0"
    "masce\0"
    "mase\0"
    "mast\0"
    "mate\0"
    "math\0"
    "matis\0"
    "matiza\0"
    "mb\0"
    "mbat\0"
    "mbil\0"
    "mbing\0"
    "mbiv\0"
    "mc\0"
    "me.\0"
    "med\0"
    "med.\0"
    "media\0"
    "medic\0"
    "medicin\0"
    "medie\0"
    "medioc\0"
    "medy\0"
    "meg\0"
    "megran\0"
    "melon\0"
    "melt\0"
    "mem\0"
    "memo\0"
    "men\0"
    "men.\0"
    "mena\0"
    "menac\0"
    "mende\0"
    "mene\0"
    "meni\0"
    "mens\0"
    "mensu\0"
    "ment\0"
    "mente\0"
    "meon\0"
    "mersa\0"
    "mes\0"
    "mesti\0"
    "meta\0"
    "metal\0"
    "mete\0"
    "methi\0"
    "metr\0"
    "metric\0"
    "metrie\0"
    "metry\0"
    "mev\0"
    "mf\0"
    "mh\0"
    "mi.\0"
    "mia\0"
    "mida\0"
    "midab\0"
    "midg\0"
    "mig\0"
    "milia\0"
    "milie\0"
    "milita\0"
    "mill\0"
    "millag\0"
    "millili\0"
    "mina\0"
    "mind\0"
    "minee\0"
    "mingl\0"
    "mingli\0"
    "mingly\0"
    "minis.\0"
    "mint\0"
    "minu\0"
    "minuter\0"
    "minutest\0"
    "miot\0"
    "mis\0"
    "miser.\0"
    "misl\0"
    "misti\0"
    "mistry\0"
    "mith\0"
    "miz\0"
    "mk\0"
    "ml\0"
    "mm\0"
    "mmab\0"
    "mmary\0"
    "mn\0"
    "mna\0"
    "mnin\0"
    "mno\0"
    "mo\0"
    "mocr\0"
    "mocrat\0"
    "mocratiz\0"
    "mod\0"
    "moelas\0"
    "mogo\0"
    "mois\0"
    "moise\0"
    "mok\0"
    "molec\0"
    "molest\0"
    "mome\0"
    "monet\0"
    "moneyl\0"
    "monge\0"
    "monia\0"
    "monism\0"
    "monist\0"
    "moniz\0"
    "monoch\0"
    "monoen\0"
    "monol\0"
    "monos\0"
    "mony.\0"
    "mor\0"
    "mora.\0"
    "moronis\0"
    "mos\0"
    "mosey\0"
    "mosp\0"
    "moth\0"
    "mothet\0"
    "mouf\0"
    "mous\0"
    "mousin\0"
    "mov\0"
    "mp\0"
    "mpara\0"
    "mparab\0"
    "mpari\0"
    "mpet\0"
    "mphas\0"
    "mpi\0"
    "mpia\0"
    "mpies\0"
    "mpin\0"
    "mpir\0"
    "mpis\0"
    "mpori\0"
    "mposite\0"
    "mpous\0"
    "mpov\0"
    "mptr\0"
    "mpy\0"
    "mr\0"
    "ms\0"
    "msh\0"
    "mshack\0"
    "msi\0"
    "mt\0"
    "mu\0"
    "mudro\0"
    "mular\0"
    "mult\0"
    "multi\0"
    "multiu\0"
    "mum\0"
    "mun\0"
    "mup\0"
    "muu\0"
    "mw\0"
    "na\0"
    "nab\0"
    "nabu\0"
    "nac.\0"
    "naca\0"
    "nact\0"
    "nager.\0"
    "nak\0"
    "nali\0"
    "nalia\0"
    "nalt\0"
    "namit\0"
    "nan\0"
    "nanci\0"
    "nanit\0"
    "nank\0"
    "narc\0"
    "narchs.\0"
    "nare\0"
    "nari\0"
    "narl\0"
    "narm\0"
    "nas\0"
    "nasc\0"
    "nasti\0"
    "nat\0"
    "natal\0"
    "natomiz\0"
    "nau\0"
    "nause\0"
    "naut\0"
    "nave\0"
    "nb\0"
    "ncar\0"
    "nces.\0"
    "ncha\0"
    "ncheo\0"
    "nchest\0"
    "nchil\0"
    "nchis\0"
    "ncin\0"
    "ncit\0"
    "ncoura\0"
    "ncr\0"
    "ncu\0"
    "ndai\0"
    "ndan\0"
    "nde\0"
    "ndest.\0"
    "ndib\0"
    "ndieck\0"
    "ndif\0"
    "ndit\0"
    "ndiz\0"
    "ndthr\0"
    "nduc\0"
    "ndur\0"
    "ndwe\0"
    "ne.\0"
    "near\0"
    "neb\0"
    "neback\0"
    "nebu\0"
    "nec\0"
    "neck\0"
    "ned\0"
    "negat\0"
    "negativ\0"
    "nege\0"
    "nela\0"
    "neliz\0"
    "nemi\0"
    "nemo\0"
    "nen\0"
    "nene\0"
    "neo\0"
    "nepo\0"
    "neq\0"
    "ner\0"
    "nerab\0"
    "nerar\0"
    "nere\0"
    "neri\0"
    "nerr\0"
    "nes\0"
    "nes.\0"
    "neski\0"
    "nesp\0"
    "nest\0"
    "nesw\0"
    "netic\0"
    "nev\0"
    "neve\0"
    "new\0"
    "nf\0"
    "nfinites\0"
    "ngab\0"
    "ngel\0"
    "ngene\0"
    "ngenes\0"
    "ngere\0"
    "ngeri\0"
    "ngha\0"
    "ngho\0"
    "ngib\0"
    "ngin\0"
    "ngit\0"
    "ngla\0"
    "ngov\0"
    "ngsh\0"
    "ngspr\0"
    "ngu\0"
    "ngum\0"
    "ngy\0"
    "nh\0"
    "nha\0"
    "nhab\0"
    "nhe\0"
    "nia\0"
    "nian\0"
    "nian.\0"
    "niap\0"
    "niba\0"
    "nibl\0"
    "nid\0"
    "nidi\0"
    "nier\0"
    "nifi\0"
    "nificat\0"
    "nigr\0"
    "nik\0"
    "nim\0"
    "nimiz\0"
    "nin\0"
    "nine.\0"
    "ning\0"
    "nio\0"
    "nis.\0"
    "nista\0"
    "nit\0"
    "nith\0"
    "nitio\0"
    "nitor\0"
    "nitr\0"
    "nj\0"
    "nk\0"
    "nkero\0"
    "nket\0"
    "nkin\0"
    "nkl\0"
    "nkrup\0"
    "nl\0"
    "nless\0"
    "nm\0"
    "nme\0"
    "nmet\0"
    "nn\0"
    "nne\0"
    "nnial\0"
    "nniv\0"
    "nobl\0"
    "noble\0"
    "noceros\0"
    "nocl\0"
    "nod\0"
    "noe\0"
    "nog\0"
    "noge\0"
    "noisi\0"
    "noli\0"
    "nologis\0"
    "nomal\0"
    "nomeno\0"
    "nomic\0"
    "nomist\0"
    "nomiz\0"
    "nomo\0"
    "nomy\0"
    "non\0"
    "nonag\0"
    "noneq\0"
    "noni\0"
    "noniso\0"
    "noniz\0"
    "nop\0"
    "nopoli\0"
    "nopoly.\0"
    "norab\0"
    "norary\0"
    "nosc\0"
    "nose\0"
    "nost\0"
    "nota\0"
    "nou\0"
    "noun\0"
    "novel\0"
    "novemb\0"
    "nowl\0"
    "np\0"
    "npi\0"
    "nprec\0"
    "nq\0"
    "nr\0"
    "nru\0"
    "ns\0"
    "nsab\0"
    "nsati\0"
    "nsc\0"
    "nsceiv\0"
    "nse\0"
    "nses\0"
    "nsid\0"
    "nsig\0"
    "nsl\0"
    "nsm\0"
    "nsmoo\0"
    "nsoc\0"
    "nspe\0"
    "nspi\0"
    "nstabl\0"
    "nt\0"
    "ntab\0"
    "nters\0"
    "nti\0"
    "ntib\0"
    "ntier\0"
    "ntif\0"
    "ntine\0"
    "nting\0"
    "ntip\0"
    "ntrep\0"
    "ntrolli\0"
    "nts\0"
    "ntume\0"
    "nua\0"
    "nud\0"
    "nuen\0"
    "nuffe\0"
    "nuin\0"
    "nuit\0"
    "num\0"
    "nume\0"
    "numi\0"
    "nun\0"
    "nuo\0"
    "nutr\0"
    "nv\0"
    "nw\0"
    "nym\0"
    "nyp\0"
    "nz\0"
    "nza\0"
    "oa\0"
    "oad\0"
    "oales\0"
    "oard\0"
    "oase\0"
    "oaste\0"
    "oati\0"
    "obab\0"
    "obar\0"
    "obel\0"
    "obi\0"
    "obin\0"
    "obing\0"
    "oblig\0"
    "obr\0"
    "obul\0"
    "oce\0"
    "och\0"
    "ochas\0"
    "ochet\0"
    "ocif\0"
    "ocil\0"
    "oclam\0"
    "ocod\0"
    "ocrac\0"
    "ocratiz\0"
    "ocre\0"
    "ocrit\0"
    "octora\0"
    "ocula\0"
    "ocure\0"
    "odded\0"
    "odelli\0"
    "odic\0"
    "odio\0"
    "oditic\0"
    "odo\0"
    "odor\0"
    "oduct.\0"
    "oducts\0"
    "oel\0"
    "oeng\0"
    "oer\0"
    "oerst\0"
    "oeta\0"
    "oev\0"
    "ofi\0"
    "ofite\0"
    "ofitt\0"
    "ogar\0"
    "ogativ\0"
    "ogato\0"
    "oge\0"
    "ogene\0"
    "ogeo\0"
    "oger\0"
    "ogie\0"
    "ogis\0"
    "ogit\0"
    "ogl\0"
    "ogly\0"
    "ogniz\0"
    "ogro\0"
    "ogui\0"
    "ogy\0"
    "ogyn\0"
    "oh\0"
    "ohab\0"
    "oi\0"
    "oices\0"
    "oider\0"
    "oiff\0"
    "oig\0"
    "oilet\0"
    "oing\0"
    "ointer\0"
    "oism\0"
    "oison\0"
    "oisten\0"
    "oiter\0"
    "oj\0"
    "ok\0"
    "oken\0"
    "okest\0"
    "okie\0"
    "ola\0"
    "olan\0"
    "olass\0"
    "old\0"
    "olde\0"
    "oler\0"
    "olesc\0"
    "olester\0"
    "olet\0"
    "olfi\0"
    "oli\0"
    "olia\0"
    "olice\0"
    "olid.\0"
    "olif\0"
    "oligopo\0"
    "olil\0"
    "oling\0"
    "olio\0"
    "olis.\0"
    "olish\0"
    "olite\0"
    "olitio\0"
    "oliv\0"
    "ollie\0"
    "ologiz\0"
    "olonom\0"
    "olor\0"
    "olpl\0"
    "olt\0"
    "olub\0"
    "olume\0"
    "olun\0"
    "olus\0"
    "olv\0"
    "oly\0"
    "omah\0"
    "omal\0"
    "omatiz\0"
    "ombe\0"
    "ombl\0"
    "ome\0"
    "omecha\0"
    "omena\0"
    "omerse\0"
    "omet\0"
    "ometry\0"
    "omia\0"
    "omic.\0"
    "omica\0"
    "omid\0"
    "omin\0"
    "omini\0"
    "ommend\0"
    "omoge\0"
    "omon\0"
    "ompi\0"
    "ompro\0"
    "on\0"
    "ona\0"
    "onac\0"
    "onan\0"
    "onc\0"
    "oncil\0"
    "ond\0"
    "ondo\0"
    "onen\0"
    "onest\0"
    "ongu\0"
    "onic\0"
    "onio\0"
    "onis\0"
    "oniu\0"
    "onkey\0"
    "onodi\0"
    "onomic\0"
    "onomy\0"
    "onorma\0"
    "onoton\0"
    "onou\0"
    "ons\0"
    "onspi\0"
    "onspira\0"
    "onsu\0"
    "onten\0"
    "onti\0"
    "ontif\0"
    "onum\0"
    "onva\0"
    "oo\0"
    "oode\0"
    "oodi\0"
    "ook\0"
    "oopi\0"
    "oord\0"
    "oost\0"
    "opa\0"
    "oped\0"
    "oper\0"
    "opera\0"
    "operag\0"
    "oph\0"
    "ophan\0"
    "opher\0"
    "oping\0"
    "opism.\0"
    "opit\0"
    "opon\0"
    "oposi\0"
    "opr\0"
    "opu\0"
    "opy\0"
    "oq\0"
    "ora\0"
    "ora.\0"
    "orag\0"
    "oraliz\0"
    "orange\0"
    "orea\0"
    "oreal\0"
    "orei\0"
    "oresh\0"
    "orest.\0"
    "orew\0"
    "orgu\0"
    "oria\0"
    "orica\0"
    "oril\0"
    "orin\0"
    "orio\0"
    "ority\0"
    "oriu\0"
    "ormi\0"
    "orne\0"
    "orof\0"
    "oroug\0"
    "orpe\0"
    "orrh\0"
    "orse\0"
    "orsen\0"
    "orst\0"
    "orthi\0"
    "orthonit\0"
    "orthri\0"
    "orthy\0"
    "ortively\0"
    "orty\0"
    "orum\0"
    "ory\0"
    "osal\0"
    "osc\0"
    "osce\0"
    "oscop\0"
    "oscopi\0"
    "oscr\0"
    "osie\0"
    "ositiv\0"
    "osito\0"
    "osity\0"
    "osiu\0"
    "osl\0"
    "oso\0"
    "ospa\0"
    "ospher\0"
    "ospo\0"
    "osta\0"
    "ostati\0"
    "ostil\0"
    "ostit\0"
    "otan\0"
    "oteleg\0"
    "oter.\0"
    "oters\0"
    "otes\0"
    "otester\0"
    "otestor\0"
    "oth\0"
    "otheos\0"
    "othesi\0"
    "othi\0"
    "otic.\0"
    "otica\0"
    "otice\0"
    "otif\0"
    "otis\0"
    "otos\0"
    "ou\0"
    "oubado\0"
    "oubl\0"
    "ouchi\0"
    "ouet\0"
    "oul\0"
    "ouncer\0"
    "ound\0"
    "ouv\0"
    "oven\0"
    "overne\0"
    "overs\0"
    "overt\0"
    "ovian.\0"
    "ovis\0"
    "oviti\0"
    "ovol\0"
    "owder\0"
    "owel\0"
    "owest\0"
    "owi\0"
    "owni\0"
    "owo\0"
    "oxidic\0"
    "oya\0"
    "pa\0"
    "paca\0"
    "pace\0"
    "pact\0"
    "pad\0"
    "pagan\0"
    "pagat\0"
    "pai\0"
    "pain\0"
    "pal\0"
    "palmat\0"
    "pana\0"
    "panel\0"
    "panty\0"
    "pany\0"
    "pap\0"
    "papu\0"
    "parabl\0"
    "parage\0"
    "paragra\0"
    "parale\0"
    "param\0"
    "parame\0"
    "pardi\0"
    "pare\0"
    "parel\0"
    "pari\0"
    "paris\0"
    "pate\0"
    "pater\0"
    "pathic\0"
    "pathy\0"
    "patric\0"
    "pav\0"
    "pay\0"
    "pb\0"
    "pd\0"
    "pe.\0"
    "pea\0"
    "pearl\0"
    "pec\0"
    "ped\0"
    "pede\0"
    "pedi\0"
    "pedia\0"
    "pedic\0"
    "pee\0"
    "peed\0"
    "peev\0"
    "pek\0"
    "pela\0"
    "pelie\0"
    "penan\0"
    "penc\0"
    "penth\0"
    "peon\0"
    "pera.\0"
    "perabl\0"
    "perag\0"
    "peri\0"
    "perist\0"
    "permal\0"
    "perme\0"
    "pern\0"
    "pero\0"
    "perti\0"
    "peru\0"
    "perv\0"
    "pet\0"
    "peten\0"
    "petiz\0"
    "pf\0"
    "pg\0"
    "ph.\0"
    "phari\0"
    "pheno\0"
    "pher\0"
    "phes.\0"
    "phic\0"
    "phie\0"
    "philant\0"
    "philatel\0"
    "phing\0"
    "phisti\0"
    "phiz\0"
    "phl\0"
    "phob\0"
    "phone\0"
    "phoni\0"
    "phor\0"
    "phs\0"
    "pht\0"
    "phu\0"
    "phy\0"
    "pia\0"
    "pian\0"
    "picad\0"
    "picie\0"
    "picy\0"
    "pid\0"
    "pida\0"
    "pide\0"
    "pidi\0"
    "piec\0"
    "pien\0"
    "pigrap\0"
    "pilo\0"
    "pin\0"
    "pin.\0"
    "pind\0"
    "pino\0"
    "pio\0"
    "pion\0"
    "pith\0"
    "pitha\0"
    "pitu\0"
    "pk\0"
    "pl\0"
    "plan\0"
    "plast\0"
    "plia\0"
    "plicab\0"
    "plier\0"
    "plig\0"
    "plin\0"
    "plinar\0"
    "ploi\0"
    "plum\0"
    "plumb\0"
    "pm\0"
    "pn\0"
    "poc\0"
    "pod.\0"
    "poem\0"
    "poet\0"
    "pog\0"
    "poin\0"
    "poinca\0"
    "point\0"
    "pole.\0"
    "polye\0"
    "polyphono\0"
    "polyt\0"
    "poni\0"
    "pop\0"
    "por\0"
    "pory\0"
    "pos\0"
    "poss\0"
    "pot\0"
    "pota\0"
    "poun\0"
    "pp\0"
    "ppara\0"
    "ppe\0"
    "pped\0"
    "ppel\0"
    "ppen\0"
    "pper\0"
    "ppet\0"
    "pposite\0"
    "pr\0"
    "praye\0"
    "preci\0"
    "preco\0"
    "preem\0"
    "prefac\0"
    "prela\0"
    "premac\0"
    "preneu\0"
    "prer\0"
    "prese\0"
    "prespli\0"
    "press\0"
    "preten\0"
    "prev\0"
    "prie\0"
    "print\0"
    "pris\0"
    "priso\0"
    "proca\0"
    "process\0"
    "procity.\0"
    "profit\0"
    "proge\0"
    "prol\0"
    "prose\0"
    "prot\0"
    "ps\0"
    "pse\0"
    "pseud\0"
    "pseudod\0"
    "pseudof\0"
    "psh\0"
    "psib\0"
    "pt\0"
    "ptab\0"
    "pte\0"
    "pth\0"
    "ptim\0"
    "ptomat\0"
    "ptrol\0"
    "ptur\0"
    "ptw\0"
    "pub\0"
    "pubesc\0"
    "pue\0"
    "puf\0"
    "pulc\0"
    "pum\0"
    "pun\0"
    "purr\0"
    "pus\0"
    "put\0"
    "pute\0"
    "puter\0"
    "putr\0"
    "putted\0"
    "puttin\0"
    "pw\0"
    "qu\0"
    "quainte\0"
    "quasi\0"
    "quasir\0"
    "quasis\0"
    "quav\0"
    "que.\0"
    "quer\0"
    "quet\0"
    "quintess\0"
    "quivar\0"
    "rab\0"
    "rabi\0"
    "rabolic\0"
    "raboloi\0"
    "rache\0"
    "rachu\0"
    "racl\0"
    "radig\0"
    "radiog\0"
    "raffi\0"
    "raft\0"
    "rai\0"
    "ralo\0"
    "ramen\0"
    "ramet\0"
    "rametriz\0"
    "rami\0"
    "ramou\0"
    "raneo\0"
    "range\0"
    "ranhas\0"
    "rani\0"
    "rano\0"
    "raor\0"
    "raper\0"
    "raphy\0"
    "rarc\0"
    "rare\0"
    "raref\0"
    "raril\0"
    "ras\0"
    "ration\0"
    "raut\0"
    "ravai\0"
    "ravel\0"
    "razie\0"
    "rb\0"
    "rbab\0"
    "rbag\0"
    "rbi\0"
    "rbif\0"
    "rbin\0"
    "rbine\0"
    "rbing.\0"
    "rbinge\0"
    "rbo\0"
    "rc\0"
    "rce\0"
    "rcen\0"
    "rcha\0"
    "rcher\0"
    "rcib\0"
    "rcit\0"
    "rcum\0"
    "rdal\0"
    "rdi\0"
    "rdia\0"
    "rdier\0"
    "rdin\0"
    "rding\0"
    "re.\0"
    "real\0"
    "rean\0"
    "rearr\0"
    "reav\0"
    "reaw\0"
    "rebrat\0"
    "recipr\0"
    "recoll\0"
    "recompe\0"
    "recre\0"
    "rectang\0"
    "red\0"
    "rede\0"
    "redis\0"
    "redit\0"
    "refac\0"
    "refe\0"
    "refer.\0"
    "refi\0"
    "refy\0"
    "regis\0"
    "reit\0"
    "reli\0"
    "relu\0"
    "renta\0"
    "rente\0"
    "reo\0"
    "repin\0"
    "reposi\0"
    "repu\0"
    "rer\0"
    "reri\0"
    "rero\0"
    "reru\0"
    "res.\0"
    "respi\0"
    "ressib\0"
    "rest\0"
    "restal\0"
    "restr\0"
    "reter\0"
    "retiz\0"
    "retri\0"
    "retribu\0"
    "reu\0"
    "reuti\0"
    "rev\0"
    "reval\0"
    "revel\0"
    "rever.\0"
    "revers\0"
    "revert\0"
    "revil\0"
    "revolu\0"
    "rewh\0"
    "rf\0"
    "rfu\0"
    "rfy\0"
    "rg\0"
    "rger\0"
    "rget\0"
    "rgic\0"
    "rgin\0"
    "rging\0"
    "rgis\0"
    "rgit\0"
    "rgl\0"
    "rgon\0"
    "rgu\0"
    "rh\0"
    "rh.\0"
    "rhal\0"
    "ria\0"
    "riab\0"
    "riag\0"
    "rial.\0"
    "rib\0"
    "riba\0"
    "ricas\0"
    "rice\0"
    "rici\0"
    "ricid\0"
    "ricie\0"
    "rico\0"
    "rider\0"
    "rienc\0"
    "rient\0"
    "rier\0"
    "riet\0"
    "rigan\0"
    "rigi\0"
    "riliz\0"
    "riman\0"
    "rimi\0"
    "rimo\0"
    "rimpe\0"
    "rina\0"
    "rina.\0"
    "rind\0"
    "rine\0"
    "ring\0"
    "rio\0"
    "riph\0"
    "riphe\0"
    "ripl\0"
    "riplic\0"
    "riq\0"
    "ris\0"
    "ris.\0"
    "risc\0"
    "rish\0"
    "risp\0"
    "ritab\0"
    "rited.\0"
    "riter.\0"
    "riters\0"
    "ritic\0"
    "ritu\0"
    "ritur\0"
    "rivel\0"
    "rivet\0"
    "rivi\0"
    "rivol\0"
    "rj\0"
    "rk.\0"
    "rket\0"
    "rkho\0"
    "rkle\0"
    "rklin\0"
    "rkrau\0"
    "rks.\0"
    "rl\0"
    "rle\0"
    "rled\0"
    "rlequ\0"
    "rlig\0"
    "rlis\0"
    "rlish\0"
    "rlo\0"
    "rm\0"
    "rmac\0"
    "rme\0"
    "rmen\0"
    "rmers\0"
    "rming\0"
    "rming.\0"
    "rmio\0"
    "rmit\0"
    "rmy\0"
    "rnar\0"
    "rnel\0"
    "rner\0"
    "rnet\0"
    "rney\0"
    "rnic\0"
    "rnis\0"
    "rnit\0"
    "rniv\0"
    "rno\0"
    "rnou\0"
    "rnu\0"
    "robl\0"
    "robot\0"
    "roc\0"
    "rocr\0"
    "roe\0"
    "roelas\0"
    "roepide\0"
    "rofe\0"
    "rofil\0"
    "rok\0"
    "roker\0"
    "role.\0"
    "romesh\0"
    "romete\0"
    "romi\0"
    "romp\0"
    "ronal\0"
    "rone\0"
    "ronis\0"
    "ronta\0"
    "room\0"
    "root\0"
    "ropel\0"
    "ropic\0"
    "rori\0"
    "roro\0"
    "rosper\0"
    "ross\0"
    "rothe\0"
    "rotron\0"
    "roty\0"
    "rova\0"
    "rovel\0"
    "rox\0"
    "rp\0"
    "rpauli\0"
    "rpea\0"
    "rpent\0"
    "rper.\0"
    "rpet\0"
    "rph\0"
    "rping\0"
    "rpo\0"
    "rr\0"
    "rrec\0"
    "rref\0"
    "rreo\0"
    "rrest\0"
    "rrio\0"
    "rriv\0"
    "rron\0"
    "rros\0"
    "rrys\0"
    "rs\0"
    "rsa\0"
    "rsati\0"
    "rsc\0"
    "rse\0"
    "rsec\0"
    "rsecr\0"
    "rser.\0"
    "rseradi\0"
    "rses\0"
    "rsev\0"
    "rsh\0"
    "rsha\0"
    "rsi\0"
    "rsib\0"
    "rson\0"
    "rsp\0"
    "rsw\0"
    "rtach\0"
    "rtag\0"
    "rteb\0"
    "rtend\0"
    "rteo\0"
    "rthou\0"
    "rti\0"
    "rtib\0"
    "rtid\0"
    "rtier\0"
    "rtig\0"
    "rtili\0"
    "rtill\0"
    "rtily\0"
    "rtist\0"
    "rtiv\0"
    "rtreu\0"
    "rtri\0"
    "rtroph\0"
    "rtsh\0"
    "rua\0"
    "ruel\0"
    "ruen\0"
    "rugl\0"
    "ruin\0"
    "rumpl\0"
    "run\0"
    "runk\0"
    "runty\0"
    "rusc\0"
    "rutin\0"
    "rve\0"
    "rveil\0"
    "rveli\0"
    "rven\0"
    "rver.\0"
    "rvest\0"
    "rvey\0"
    "rvic\0"
    "rviv\0"
    "rvo\0"
    "rw\0"
    "ryc\0"
    "rynge\0"
    "ryt\0"
    "rzsc\0"
    "sa\0"
    "sab\0"
    "sack\0"
    "sacri\0"
    "sact\0"
    "sai\0"
    "salar\0"
    "salesc\0"
    "salesw\0"
    "salm\0"
    "salo\0"
    "salt\0"
    "sanc\0"
    "sande\0"
    "sap\0"
    "saparil\0"
    "sata\0"
    "satio\0"
    "satu\0"
    "sau\0"
    "savor\0"
    "saw\0"
    "sb\0"
    "scant\0"
    "scap\0"
    "scaper\0"
    "scatol\0"
    "scav\0"
    "sced\0"
    "scei\0"
    "sces\0"
    "sch\0"
    "schitz\0"
    "scho\0"
    "schroding\0"
    "scie\0"
    "scind\0"
    "sciutt\0"
    "scle\0"
    "scli\0"
    "scof\0"
    "scopy\0"
    "scoura\0"
    "scraper.\0"
    "scu\0"
    "scyth\0"
    "sd\0"
    "se.\0"
    "sea\0"
    "seas\0"
    "seaw\0"
    "seco\0"
    "sect\0"
    "sed\0"
    "sede\0"
    "sedl\0"
    "seg\0"
    "segr\0"
    "sei\0"
    "sele\0"
    "self\0"
    "selv\0"
    "semaph\0"
    "seme\0"
    "semest\0"
    "semitic\0"
    "semol\0"
    "senat\0"
    "senc\0"
    "send\0"
    "sened\0"
    "seng\0"
    "senin\0"
    "sentd\0"
    "sentl\0"
    "sepa\0"
    "septemb\0"
    "ser.\0"
    "serl\0"
    "sero\0"
    "servo\0"
    "ses\0"
    "sesh\0"
    "sest\0"
    "seum\0"
    "sev\0"
    "seven\0"
    "sewi\0"
    "sex\0"
    "sf\0"
    "sg\0"
    "sh\0"
    "sh.\0"
    "sher\0"
    "shev\0"
    "shin\0"
    "shio\0"
    "ship\0"
    "shiv\0"
    "sho\0"
    "shoest\0"
    "shold\0"
    "shon\0"
    "shor\0"
    "short\0"
    "shw\0"
    "sib\0"
    "sicc\0"
    "side.\0"
    "sided.\0"
    "sides\0"
    "sidest\0"
    "sidesw\0"
    "sidi\0"
    "sidiz\0"
    "signa\0"
    "sile\0"
    "sily\0"
    "sin\0"
    "sina\0"
    "sine.\0"
    "sing\0"
    "sio\0"
    "sion\0"
    "siona\0"
    "sir\0"
    "sira\0"
    "siresid\0"
    "sis\0"
    "sitio\0"
    "siu\0"
    "siv\0"
    "siz\0"
    "sk\0"
    "ske\0"
    "sket\0"
    "skine\0"
    "sking\0"
    "skysc\0"
    "sl\0"
    "slat\0"
    "sle\0"
    "slith\0"
    "slovakia\0"
    "sm\0"
    "sma\0"
    "small\0"
    "sman\0"
    "smel\0"
    "smen\0"
    "smith\0"
    "smold\0"
    "sn\0"
    "so\0"
    "soce\0"
    "soft\0"
    "sogamy\0"
    "solab\0"
    "sold\0"
    "solic\0"
    "solute\0"
    "solv\0"
    "som\0"
    "son.\0"
    "sona\0"
    "song\0"
    "sop\0"
    "sophic\0"
    "sophiz\0"
    "sophy\0"
    "sorc\0"
    "sord\0"
    "sov\0"
    "sovi\0"
    "spa\0"
    "space\0"
    "spacin\0"
    "spai\0"
    "span\0"
    "specio\0"
    "spend\0"
    "speo\0"
    "sper\0"
    "sphe\0"
    "spher\0"
    "sphero\0"
    "spho\0"
    "spicil\0"
    "spil\0"
    "sping\0"
    "spio\0"
    "sply\0"
    "spokesw\0"
    "spon\0"
    "spor\0"
    "sportsc\0"
    "sportsw\0"
    "spot\0"
    "squall\0"
    "squito\0"
    "sr\0"
    "ss\0"
    "ssa\0"
    "ssachu\0"
    "ssas\0"
    "ssc\0"
    "ssel\0"
    "sseng\0"
    "sses.\0"
    "sset\0"
    "sshat\0"
    "ssi\0"
    "ssian.\0"
    "ssie\0"
    "ssier\0"
    "ssignab\0"
    "ssily\0"
    "ssl\0"
    "ssli\0"
    "ssn\0"
    "sspend\0"
    "sst\0"
    "ssura\0"
    "ssw\0"
    "st.\0"
    "stag\0"
    "stal\0"
    "stami\0"
    "stamp\0"
    "stand\0"
    "stantshi\0"
    "stap\0"
    "startli\0"
    "stat.\0"
    "stati\0"
    "stb\0"
    "sted\0"
    "sterni\0"
    "stero\0"
    "stew\0"
    "stewa\0"
    "sthe\0"
    "sti\0"
    "sti.\0"
    "stia\0"
    "stic\0"
    "stick\0"
    "stie\0"
    "stif\0"
    "sting\0"
    "stir\0"
    "stle\0"
    "stock\0"
    "stoma\0"
    "stone\0"
    "stop\0"
    "storab\0"
    "store\0"
    "str\0"
    "strad\0"
    "stratag\0"
    "stratu\0"
    "stray\0"
    "stribut\0"
    "strid\0"
    "stry\0"
    "stscr\0"
    "stupid\0"
    "stw\0"
    "sty\0"
    "stylis\0"
    "su\0"
    "sual\0"
    "sub\0"
    "sug\0"
    "suis\0"
    "suit\0"
    "sul\0"
    "sum\0"
    "sumi\0"
    "sun\0"
    "supere\0"
    "sur\0"
    "sv\0"
    "sw\0"
    "swimm\0"
    "swo\0"
    "sy\0"
    "syc\0"
    "syl\0"
    "sync\0"
    "syno\0"
    "syrin\0"
    "sythi\0"
    "ta\0"
    "ta.\0"
    "tab\0"
    "tables\0"
    "tabolism\0"
    "taboliz\0"
    "taci\0"
    "tado\0"
    "taf\0"
    "tagon.\0"
    "tailo\0"
    "tal\0"
    "tala\0"
    "talen\0"
    "tali\0"
    "talk\0"
    "talka\0"
    "tallis\0"
    "talog\0"
    "tamin\0"
    "tamo\0"
    "tande\0"
    "tanta\0"
    "tapath\0"
    "taper\0"
    "tapl\0"
    "tara\0"
    "tarc\0"
    "tare\0"
    "tariz\0"
    "tarrh\0"
    "tase\0"
    "tasy\0"
    "tatic\0"
    "tatur\0"
    "taun\0"
    "tav\0"
    "taw\0"
    "taxis\0"
    "tb\0"
    "tc\0"
    "tch\0"
    "tchc\0"
    "tchet\0"
    "tchier\0"
    "tcr\0"
    "td\0"
    "te.\0"
    "teacher.\0"
    "teadi\0"
    "teat\0"
    "tece\0"
    "tect\0"
    "ted\0"
    "tedi\0"
    "tee\0"
    "teg\0"
    "teger\0"
    "tegi\0"
    "tel.\0"
    "teleg\0"
    "telero\0"
    "teli\0"
    "tels\0"
    "tema\0"
    "temat\0"
    "tenan\0"
    "tenc\0"
    "tend\0"
    "tenes\0"
    "tent\0"
    "tentag\0"
    "teo\0"
    "tep\0"
    "tepe\0"
    "terc\0"
    "terd\0"
    "tergei\0"
    "teri\0"
    "teric.\0"
    "teries\0"
    "teris\0"
    "teriza\0"
    "ternit\0"
    "terv\0"
    "tes.\0"
    "tess\0"
    "tess.\0"
    "tesses\0"
    "tethe\0"
    "teu\0"
    "tex\0"
    "tey\0"
    "tf\0"
    "tg\0"
    "th.\0"
    "thalam\0"
    "than\0"
    "the\0"
    "thea\0"
    "theas\0"
    "theat\0"
    "theis\0"
    "thet\0"
    "thic.\0"
    "thica\0"
    "thil\0"
    "think\0"
    "thl\0"
    "thode\0"
    "thodic\0"
    "thodon\0"
    "thogeni\0"
    "thoker\0"
    "thoo\0"
    "thorit\0"
    "thoriz\0"
    "ths\0"
    "thylan\0"
    "thysc\0"
    "tia\0"
    "tiab\0"
    "tian.\0"
    "tiato\0"
    "tib\0"
    "tick\0"
    "tico\0"
    "ticu\0"
    "tidi\0"
    "tien\0"
    "tif\0"
    "tify\0"
    "tig\0"
    "tigu\0"
    "tillin\0"
    "tim\0"
    "timp\0"
    "timul\0"
    "tin\0"
    "tina\0"
    "tine.\0"
    "tini\0"
    "tinom\0"
    "tio\0"
    "tioc\0"
    "tionee\0"
    "tiq\0"
    "tisa\0"
    "tise\0"
    "tism\0"
    "tiso\0"
    "tisp\0"
    "tistica\0"
    "titl\0"
    "tiu\0"
    "tiv\0"
    "tiva\0"
    "tiz\0"
    "tiza\0"
    "tizen\0"
    "tl\0"
    "tla\0"
    "tlan\0"
    "tle.\0"
    "tled\0"
    "tles.\0"
    "tlet.\0"
    "tlier\0"
    "tlo\0"
    "tm\0"
    "tme\0"
    "tn\0"
    "to\0"
    "tob\0"
    "tocrat\0"
    "todo\0"
    "tof\0"
    "togr\0"
    "toic\0"
    "tology\0"
    "toma\0"
    "tomb\0"
    "tomy\0"
    "tonali\0"
    "tonat\0"
    "tono\0"
    "tony\0"
    "tora\0"
    "torie\0"
    "toriz\0"
    "tos\0"
    "totic\0"
    "tour\0"
    "tout\0"
    "towar\0"
    "tp\0"
    "tra\0"
    "trab\0"
    "trach\0"
    "traci\0"
    "tracit\0"
    "tracte\0"
    "traitor\0"
    "tras\0"
    "traven\0"
    "travers\0"
    "traversab\0"
    "traves\0"
    "treache\0"
    "tref\0"
    "trem\0"
    "tremi\0"
    "tria\0"
    "trial.\0"
    "trices\0"
    "tricia\0"
    "trics\0"
    "trim\0"
    "triv\0"
    "trofic.\0"
    "trofit\0"
    "troleum\0"
    "tromi\0"
    "troni\0"
    "trony\0"
    "trophe\0"
    "tropis\0"
    "tropoles\0"
    "tropolis\0"
    "tropolit\0"
    "trosp\0"
    "trov\0"
    "trui\0"
    "trus\0"
    "ts\0"
    "tsc\0"
    "tschie\0"
    "tsh\0"
    "tsw\0"
    "tt\0"
    "ttes\0"
    "tto\0"
    "ttribut\0"
    "ttu\0"
    "tu\0"
    "tua\0"
    "tuar\0"
    "tubi\0"
    "tud\0"
    "tue\0"
    "tuf\0"
    "tui\0"
    "tum\0"
    "tunis\0"
    "tup.\0"
    "ture\0"
    "turi\0"
    "turis\0"
    "turnar\0"
    "turo\0"
    "tury\0"
    "tus\0"
    "tv\0"
    "tw\0"
    "twa\0"
    "twh\0"
    "twis\0"
    "two\0"
    "ty\0"
    "tya\0"
    "tyl\0"
    "typal\0"
    "type\0"
    "typh\0"
    "tz\0"
    "tze\0"
    "uab\0"
    "uac\0"
    "uadrati\0"
    "uadratu\0"
    "uana\0"
    "uani\0"
    "uarant\0"
    "uard\0"
    "uari\0"
    "uart\0"
    "uat\0"
    "uav\0"
    "ube\0"
    "ubel\0"
    "uber\0"
    "ubero\0"
    "ubi\0"
    "ubing\0"
    "uble.\0"
    "uca\0"
    "ucib\0"
    "ucit\0"
    "ucle\0"
    "ucr\0"
    "ucu\0"
    "ucy\0"
    "udd\0"
    "uder\0"
    "udest\0"
    "udev\0"
    "udic\0"
    "udied\0"
    "udies\0"
    "udis\0"
    "udit\0"
    "udon\0"
    "udony\0"
    "udsi\0"
    "udu\0"
    "ueam\0"
    "uene\0"
    "uens\0"
    "uente\0"
    "ueril\0"
    "ufa\0"
    "ufl\0"
    "ughen\0"
    "ugin\0"
    "ui\0"
    "uiliz\0"
    "uin\0"
    "uing\0"
    "uirm\0"
    "uita\0"
    "uiv\0"
    "uiver.\0"
    "uj\0"
    "uk\0"
    "ula\0"
    "ulab\0"
    "ulati\0"
    "ulch\0"
    "ulche\0"
    "ulder\0"
    "ule\0"
    "ulen\0"
    "ulgi\0"
    "uli\0"
    "ulia\0"
    "uling\0"
    "ulish\0"
    "ullar\0"
    "ullib\0"
    "ullis\0"
    "ulm\0"
    "ulo\0"
    "uls\0"
    "ulses\0"
    "ulti\0"
    "ultra\0"
    "ultu\0"
    "ulu\0"
    "ulul\0"
    "ulv\0"
    "umab\0"
    "umbi\0"
    "umbly\0"
    "umi\0"
    "uming\0"
    "umoro\0"
    "ump\0"
    "unat\0"
    "une\0"
    "uner\0"
    "uni\0"
    "unim\0"
    "unin\0"
    "unish\0"
    "univ\0"
    "uns\0"
    "unsw\0"
    "untab\0"
    "unter.\0"
    "untes\0"
    "unu\0"
    "uny\0"
    "unz\0"
    "uors\0"
    "uos\0"
    "uou\0"
    "upe\0"
    "upers\0"
    "upia\0"
    "uping\0"
    "upl\0"
    "upp\0"
    "upport\0"
    "uptib\0"
    "uptu\0"
    "ura\0"
    "ura.\0"
    "urag\0"
    "ural.\0"
    "uras\0"
    "urbe\0"
    "urc\0"
    "urd\0"
    "ureat\0"
    "urfer\0"
    "urfr\0"
    "urial.\0"
    "urif\0"
    "urific\0"
    "urin\0"
    "urio\0"
    "urit\0"
    "uriz\0"
    "url\0"
    "urling.\0"
    "urno\0"
    "uros\0"
    "urpe\0"
    "urpi\0"
    "urser\0"
    "urtes\0"
    "urthe\0"
    "urti\0"
    "urtie\0"
    "uru\0"
    "us\0"
    "usad\0"
    "usan\0"
    "usap\0"
    "usc\0"
    "usci\0"
    "usea\0"
    "user.\0"
    "usia\0"
    "usic\0"
    "uslin\0"
    "usp\0"
    "ussl\0"
    "ustere\0"
    "ustr\0"
    "usu\0"
    "usur\0"
    "utab\0"
    "utat\0"
    "ute.\0"
    "utel\0"
    "uten\0"
    "uteni\0"
    "uti\0"
    "utiliz\0"
    "utine\0"
    "uting\0"
    "utiona\0"
    "utis\0"
    "utiz\0"
    "utl\0"
    "utof\0"
    "utog\0"
    "utomatic\0"
    "uton\0"
    "utou\0"
    "uts\0"
    "uu\0"
    "uum\0"
    "uv\0"
    "uxu\0"
    "uze\0"
    "va\0"
    "va.\0"
    "vab\0"
    "vacil\0"
    "vacu\0"
    "vag\0"
    "vage\0"
    "vaguer\0"
    "valie\0"
    "valo\0"
    "valu\0"
    "vamo\0"
    "vaniz\0"
    "vapi\0"
    "varied\0"
    "vat\0"
    "vativ\0"
    "vaudev\0"
    "ve.\0"
    "ved\0"
    "veg\0"
    "vel.\0"
    "velli\0"
    "velo\0"
    "vely\0"
    "venom\0"
    "venue\0"
    "verd\0"
    "vere.\0"
    "vereig\0"
    "verel\0"
    "verely.\0"
    "veren\0"
    "verenc\0"
    "veres\0"
    "verie\0"
    "vermin\0"
    "verse\0"
    "verth\0"
    "ves\0"
    "ves.\0"
    "veste\0"
    "vestite\0"
    "vete\0"
    "veter\0"
    "vety\0"
    "viali\0"
    "vian\0"
    "vide.\0"
    "vided\0"
    "viden\0"
    "vides\0"
    "vidi\0"
    "vif\0"
    "vign\0"
    "vik\0"
    "vil\0"
    "vilit\0"
    "viliz\0"
    "vin\0"
    "vina\0"
    "vinc\0"
    "vind\0"
    "ving\0"
    "viol\0"
    "vior\0"
    "viou\0"
    "vip\0"
    "viro\0"
    "visit\0"
    "viso\0"
    "visu\0"
    "viti\0"
    "vitr\0"
    "vity\0"
    "viv\0"
    "vivipar\0"
    "vo.\0"
    "voi\0"
    "voicep\0"
    "voirdu\0"
    "vok\0"
    "vola\0"
    "vole\0"
    "volt\0"
    "volv\0"
    "vomi\0"
    "vorab\0"
    "vori\0"
    "vory\0"
    "vota\0"
    "votee\0"
    "vv\0"
    "vy\0"
    "wabl\0"
    "wac\0"
    "wager\0"
    "wago\0"
    "wait\0"
    "wal.\0"
    "wam\0"
    "wart\0"
    "wast\0"
    "wastewa\0"
    "wate\0"
    "waveg\0"
    "waver\0"
    "wb\0"
    "wc\0"
    "wearie\0"
    "weath\0"
    "wedn\0"
    "weekn\0"
    "weet\0"
    "weev\0"
    "well\0"
    "wer\0"
    "west\0"
    "wev\0"
    "whi\0"
    "wi\0"
    "widesp\0"
    "wil\0"
    "willin\0"
    "winde\0"
    "wing\0"
    "wir\0"
    "wise\0"
    "with\0"
    "wiz\0"
    "wk\0"
    "wles\0"
    "wlin\0"
    "wno\0"
    "wo\0"
    "woken\0"
    "wom\0"
    "woven\0"
    "wp\0"
    "wra\0"
    "wraparo\0"
    "wri\0"
    "writa\0"
    "writer.\0"
    "wsh\0"
    "wsl\0"
    "wspe\0"
    "wst\0"
    "wt\0"
    "wy\0"
    "xa\0"
    "xace\0"
    "xago\0"
    "xam\0"
    "xap\0"
    "xas\0"
    "xc\0"
    "xe\0"
    "xecuto\0"
    "xed\0"
    "xeri\0"
    "xero\0"
    "xh\0"
    "xhi\0"
    "xhil\0"
    "xhu\0"
    "xi\0"
    "xia\0"
    "xic\0"
    "xidi\0"
    "xime\0"
    "ximiz\0"
    "xo\0"
    "xob\0"
    "xp\0"
    "xpand\0"
    "xpecto\0"
    "xped\0"
    "xq\0"
    "xquis\0"
    "xt\0"
    "xti\0"
    "xu\0"
    "xua\0"
    "xx\0"
    "yac\0"
    "yar\0"
    "yat\0"
    "yb\0"
    "yc\0"
    "yce\0"
    "ycer\0"
    "ych\0"
    "yche\0"
    "yched\0"
    "ycom\0"
    "ycot\0"
    "yd\0"
    "yee\0"
    "yer\0"
    "yerf\0"
    "yes\0"
    "yestery\0"
    "yet\0"
    "ygi\0"
    "yh\0"
    "yi\0"
    "yla\0"
    "yllabl\0"
    "ylo\0"
    "ylu\0"
    "ymbol\0"
    "yme\0"
    "ymetry\0"
    "ympa\0"
    "ynchr\0"
    "ynd\0"
    "yng\0"
    "ynic\0"
    "ynx\0"
    "yo\0"
    "yod\0"
    "yog\0"
    "yom\0"
    "yonet\0"
    "yons\0"
    "yos\0"
    "yped\0"
    "yper\0"
    "ypi\0"
    "ypo\0"
    "ypoc\0"
    "ypta\0"
    "ypu\0"
    "yram\0"
    "yria\0"
    "yro\0"
    "yrr\0"
    "ysc\0"
    "yse\0"
    "ysica\0"
    "ysio\0"
    "ysis\0"
    "yso\0"
    "yss\0"
    "yst\0"
    "ysta\0"
    "ystro\0"
    "ysur\0"
    "ythin\0"
    "ytic\0"
    "yw\0"
    "za\0"
    "zab\0"
    "zar\0"
    "zb\0"
    "ze\0"
    "zen\0"
    "zep\0"
    "zer\0"
    "zero\0"
    "zet\0"
    "zi\0"
    "zian.\0"
    "zil\0"
    "zis\0"
    "zl\0"
    "zm\0"
    "zo\0"
    "zom\0"
    "zool\0"
    "zophr\0"
    "zte\0"
    "zz\0"
    "zzw\0"
    "zzy\0";

static const uint16_t g_en_us_pattern_offsets[] = {
    0, 5, 12, 17, 22, 28, 33, 38, 44, 49, 55, 62, 67, 74, 80, 85, 90, 95, 102, 108,
    113, 118, 123, 128, 134, 140, 145, 151, 157, 164, 169, 176, 183, 189, 196, 203, 208, 214, 218, 226,
    230, 236, 241, 248, 253, 261, 268, 273, 279, 285, 293, 299, 307, 312, 318, 323, 329, 336, 342, 346,
    351, 355, 360, 366, 372, 377, 382, 389, 395, 399, 406, 410, 417, 421, 430, 435, 440, 448, 452, 459,
    463, 469, 475, 482, 488, 493, 498, 503, 510, 516, 520, 526, 532, 539, 546, 551, 556, 561, 567, 574,
    580, 585, 590, 596, 601, 608, 612, 618, 623, 628, 633, 639, 644, 649, 654, 661, 668, 674, 679, 686,
    692, 696, 702, 710, 715, 720, 725, 730, 736, 741, 746, 751, 757, 763, 769, 776, 780, 787, 793, 801,
    808, 815, 820, 828, 835, 841, 847, 853, 860, 866, 871, 878, 885, 889, 894, 900, 907, 912, 917, 922,
    926, 932, 937, 942, 949, 955, 962, 967, 973, 978, 985, 993, 1000, 1006, 1011, 1017, 1026, 1034, 1039, 1045,
    1052, 1057, 1065, 1070, 1076, 1081, 1087, 1093, 1098, 1104, 1110, 1116, 1122, 1129, 1136, 1143, 1150, 1157, 1164, 1169,
    1176, 1180, 1184, 1190, 1197, 1204, 1208, 1215, 1219, 1223, 1232, 1236, 1244, 1251, 1255, 1259, 1264, 1270, 1276, 1282,
    1288, 1293, 1299, 1306, 1311, 1317, 1325, 1330, 1337, 1343, 1350, 1355, 1362, 1367, 1372, 1377, 1381, 1386, 1391, 1398,
    1404, 1411, 1417, 1423, 1427, 1431, 1436, 1441, 1445, 1451, 1456, 1463, 1469, 1476, 1483, 1487, 1493, 1498, 1504, 1509,
    1515, 1521, 1527, 1532, 1538, 1542, 1547, 1552, 1557, 1563, 1569, 1574, 1579, 1582, 1588, 1594, 1598, 1603, 1609, 1615,
    1620, 1625, 1630, 1635, 1640, 1646, 1651, 1655, 1660, 1665, 1669, 1675, 1678, 1682, 1689, 1694, 1699, 1705, 1710, 1715,
    1719, 1723, 1727, 1731, 1736, 1742, 1748, 1753, 1757, 1761, 1765, 1769, 1773, 1776, 1780, 1785, 1790, 1794, 1800, 1805,
    1811, 1814, 1819, 1824, 1829, 1834, 1839, 1843, 1849, 1856, 1861, 1865, 1871, 1876, 1882, 1888, 1892, 1898, 1903, 1908,
    1914, 1919, 1924, 1928, 1933, 1938, 1944, 1950, 1957, 1963, 1971, 1977, 1982, 1987, 1993, 1998, 2004, 2008, 2013, 2019,
    2025, 2028, 2034, 2040, 2047, 2052, 2058, 2064, 2070, 2074, 2080, 2086, 2091, 2097, 2102, 2107, 2114, 2119, 2123, 2129,
    2134, 2139, 2145, 2150, 2156, 2162, 2168, 2173, 2178, 2184, 2189, 2194, 2200, 2206, 2210, 2216, 2221, 2227, 2232, 2238,
    2244, 2249, 2254, 2260, 2265, 2271, 2276, 2282, 2288, 2294, 2300, 2307, 2312, 2317, 2322, 2327, 2332, 2337, 2340, 2345,
    2350, 2358, 2364, 2370, 2375, 2382, 2390, 2395, 2401, 2407, 2411, 2416, 2422, 2428, 2434, 2440, 2444, 2449, 2452, 2458,
    2464, 2471, 2476, 2484, 2490, 2495, 2500, 2507, 2514, 2519, 2524, 2529, 2535, 2542, 2549, 2556, 2561, 2567, 2572, 2578,
    2584, 2589, 2594, 2598, 2604, 2610, 2616, 2621, 2628, 2633, 2638, 2643, 2648, 2654, 2659, 2663, 2667, 2675, 2680, 2685,
    2690, 2694, 2699, 2705, 2710, 2716, 2721, 2726, 2731, 2736, 2740, 2745, 2750, 2755, 2761, 2766, 2772, 2781, 2785, 2791,
    2796, 2802, 2807, 2812, 2818, 2824, 2830, 2836, 2842, 2849, 2855, 2860, 2864, 2870, 2876, 2884, 2889, 2895, 2900, 2905,
    2910, 2915, 2920, 2928, 2934, 2939, 2944, 2951, 2956, 2961, 2965, 2971, 2976, 2982, 2987, 2994, 2999, 3003, 3008, 3013,
    3018, 3024, 3028, 3032, 3037, 3044, 3049, 3053, 3059, 3064, 3068, 3074, 3080, 3085, 3089, 3094, 3099, 3105, 3111, 3117,
    3123, 3127, 3133, 3138, 3144, 3149, 3154, 3159, 3163, 3168, 3172, 3177, 3182, 3187, 3191, 3195, 3201, 3206, 3210, 3218,
    3225, 3230, 3235, 3242, 3247, 3252, 3258, 3264, 3272, 3278, 3282, 3288, 3292, 3295, 3299, 3304, 3310, 3315, 3318, 3322,
    3327, 3332, 3336, 3341, 3346, 3351, 3356, 3361, 3365, 3370, 3375, 3379, 3385, 3390, 3394, 3399, 3405, 3409, 3415, 3420,
    3425, 3431, 3435, 3440, 3443, 3446, 3450, 3454, 3460, 3464, 3469, 3474, 3478, 3482, 3488, 3495, 3501, 3506, 3512, 3518,
    3523, 3529, 3535, 3540, 3544, 3550, 3557, 3562, 3568, 3573, 3576, 3579, 3582, 3588, 3594, 3599, 3604, 3610, 3616, 3621,
    3625, 3631, 3637, 3640, 3643, 3648, 3652, 3657, 3661, 3667, 3673, 3678, 3684, 3688, 3693, 3698, 3703, 3708, 3713, 3719,
    3723, 3728, 3733, 3738, 3744, 3750, 3753, 3758, 3764, 3770, 3773, 3778, 3781, 3785, 3789, 3793, 3800, 3805, 3810, 3815,
    3819, 3825, 3830, 3836, 3843, 3850, 3856, 3864, 3869, 3874, 3881, 3887, 3892, 3899, 3902, 3905, 3909, 3913, 3916, 3922,
    3927, 3932, 3938, 3944, 3948, 3952, 3958, 3964, 3971, 3976, 3981, 3986, 3992, 3998, 4004, 4010, 4015, 4021, 4027, 4034,
    4041, 4046, 4052, 4057, 4063, 4069, 4072, 4077, 4082, 4089, 4094, 4100, 4104, 4109, 4115, 4119, 4124, 4129, 4133, 4138,
    4143, 4148, 4153, 4157, 4163, 4168, 4174, 4181, 4186, 4190, 4195, 4199, 4202, 4206, 4211, 4218, 4225, 4229, 4235, 4240,
    4246, 4252, 4258, 4264, 4270, 4277, 4282, 4289, 4297, 4303, 4308, 4313, 4318, 4322, 4327, 4333, 4338, 4341, 4345, 4350,
    4355, 4359, 4364, 4371, 4377, 4381, 4386, 4391, 4395, 4399, 4404, 4410, 4416, 4421, 4427, 4432, 4437, 4442, 4447, 4452,
    4458, 4464, 4470, 4474, 4480, 4484, 4487, 4491, 4494, 4499, 4508, 4514, 4520, 4525, 4530, 4535, 4539, 4542, 4545, 4550,
    4554, 4558, 4563, 4567, 4573, 4578, 4583, 4589, 4595, 4600, 4605, 4610, 4615, 4620, 4627, 4633, 4638, 4643, 4649, 4654,
    4661, 4665, 4670, 4675, 4680, 4685, 4688, 4694, 4700, 4707, 4713, 4718, 4724, 4729, 4733, 4738, 4743, 4748, 4754, 4761,
    4768, 4775, 4781, 4787, 4793, 4798, 4801, 4804, 4809, 4815, 4821, 4825, 4830, 4836, 4842, 4850, 4855, 4859, 4863, 4867,
    4871, 4877, 4882, 4889, 4895, 4900, 4905, 4910, 4914, 4919, 4924, 4930, 4938, 4944, 4948, 4954, 4958, 4964, 4970, 4975,
    4978, 4982, 4985, 4989, 4993, 4998, 5002, 5006, 5013, 5017, 5022, 5027, 5032, 5037, 5041, 5047, 5053, 5058, 5062, 5067,
    5071, 5074, 5077, 5080, 5085, 5090, 5094, 5099, 5106, 5112, 5118, 5124, 5130, 5137, 5145, 5151, 5155, 5160, 5169, 5174,
    5180, 5186, 5191, 5195, 5200, 5206, 5213, 5219, 5226, 5232, 5238, 5242, 5248, 5253, 5260, 5265, 5269, 5274, 5279, 5284,
    5288, 5293, 5298, 5305, 5310, 5314, 5319, 5324, 5330, 5335, 5341, 5347, 5352, 5356, 5362, 5367, 5371, 5377, 5381, 5384,
    5388, 5393, 5397, 5401, 5404, 5408, 5412, 5417, 5424, 5430, 5435, 5440, 5444, 5449, 5453, 5460, 5465, 5472, 5480, 5484,
    5489, 5495, 5500, 5506, 5510, 5515, 5520, 5524, 5529, 5535, 5541, 5547, 5551, 5556, 5561, 5566, 5570, 5573, 5576, 5580,
    5585, 5591, 5596, 5602, 5608, 5613, 5617, 5621, 5625, 5628, 5631, 5634, 5638, 5643, 5647, 5651, 5655, 5660, 5665, 5671,
    5677, 5683, 5688, 5693, 5698, 5702, 5706, 5711, 5715, 5722, 5726, 5729, 5732, 5739, 5744, 5748, 5754, 5759, 5766, 5771,
    5778, 5783, 5791, 5798, 5803, 5808, 5815, 5819, 5822, 5826, 5830, 5834, 5839, 5843, 5846, 5850, 5856, 5860, 5865, 5871,
    5877, 5883, 5888, 5892, 5897, 5903, 5907, 5914, 5918, 5923, 5926, 5929, 5932, 5936, 5941, 5946, 5950, 5955, 5959, 5965,
    5970, 5976, 5980, 5986, 5992, 5998, 6003, 6010, 6015, 6020, 6026, 6032, 6038, 6043, 6048, 6054, 6059, 6064, 6069, 6073,
    6079, 6085, 6091, 6096, 6100, 6106, 6111, 6116, 6119, 6125, 6131, 6136, 6141, 6145, 6150, 6156, 6161, 6165, 6172, 6178,
    6182, 6187, 6196, 6203, 6209, 6214, 6219, 6225, 6231, 6237, 6242, 6248, 6255, 6261, 6266, 6272, 6278, 6282, 6288, 6294,
    6299, 6303, 6308, 6314, 6318, 6322, 6327, 6332, 6337, 6341, 6346, 6351, 6357, 6362, 6367, 6372, 6376, 6381, 6386, 6391,
    6396, 6404, 6410, 6414, 6419, 6423, 6428, 6433, 6437, 6442, 6446, 6450, 6455, 6460, 6464, 6467, 6473, 6477, 6482, 6488,
    6493, 6499, 6507, 6512, 6519, 6526, 6531, 6536, 6541, 6546, 6552, 6557, 6561, 6566, 6571, 6576, 6581, 6585, 6588, 6593,
    6596, 6600, 6604, 6608, 6613, 6618, 6623, 6628, 6634, 6639, 6644, 6649, 6654, 6657, 6662, 6668, 6673, 6678, 6682, 6687,
    6692, 6698, 6705, 6710, 6716, 6721, 6728, 6733, 6738, 6744, 6749, 6754, 6759, 6763, 6767, 6773, 6779, 6785, 6791, 6798,
    6803, 6809, 6814, 6819, 6825, 6832, 6837, 6842, 6848, 6853, 6858, 6863, 6868, 6874, 6879, 6884, 6889, 6894, 6899, 6904,
    6909, 6915, 6919, 6923, 6928, 6933, 6939, 6944, 6951, 6956, 6962, 6968, 6973, 6979, 6985, 6990, 6996, 7001, 7008, 7013,
    7018, 7024, 7029, 7033, 7039, 7045, 7052, 7058, 7064, 7069, 7074, 7079, 7085, 7091, 7097, 7103, 7108, 7114, 7119, 7124,
    7129, 7135, 7140, 7145, 7150, 7154, 7158, 7163, 7168, 7173, 7178, 7185, 7192, 7197, 7202, 7207, 7211, 7215, 7219, 7226,
    7230, 7234, 7240, 7244, 7249, 7255, 7259, 7263, 7268, 7273, 7277, 7281, 7286, 7292, 7297, 7303, 7312, 7317, 7325, 7330,
    7334, 7340, 7347, 7353, 7359, 7364, 7370, 7375, 7381, 7386, 7392, 7395, 7401, 7407, 7411, 7416, 7422, 7427, 7434, 7438,
    7443, 7448, 7454, 7459, 7465, 7471, 7477, 7483, 7489, 7495, 7502, 7508, 7514, 7519, 7525, 7531, 7536, 7540, 7544, 7549,
    7556, 7562, 7568, 7574, 7580, 7585, 7590, 7595, 7600, 7606, 7610, 7616, 7622, 7628, 7633, 7637, 7642, 7647, 7652, 7657,
    7661, 7667, 7673, 7678, 7683, 7687, 7692, 7698, 7702, 7709, 7716, 7720, 7725, 7731, 7736, 7741, 7745, 7750, 7756, 7762,
    7769, 7776, 7783, 7787, 7792, 7798, 7802, 7807, 7812, 7819, 7826, 7831, 7836, 7843, 7848, 7854, 7859, 7864, 7870, 7875,
    7881, 7885, 7892, 7898, 7905, 7911, 7915, 7921, 7927, 7933, 7939, 7944, 7950, 7955, 7961, 7968, 7973, 7979, 7983, 7988,
    7994, 7999, 8005, 8014, 8019, 8025, 8030, 8036, 8041, 8048, 8055, 8059, 8065, 8070, 8075, 8081, 8087, 8093, 8099, 8104,
    8109, 8113, 8116, 8123, 8127, 8131, 8136, 8140, 8145, 8151, 8156, 8161, 8166, 8172, 8177, 8183, 8189, 8195, 8201, 8206,
    8212, 8216, 8221, 8226, 8231, 8236, 8241, 8245, 8249, 8254, 8259, 8263, 8268, 8274, 8279, 8283, 8287, 8292, 8296, 8299,
    8304, 8309, 8314, 8318, 8323, 8329, 8334, 8340, 8344, 8350, 8355, 8361, 8366, 8372, 8375, 8378, 8382, 8387, 8393, 8397,
    8404, 8409, 8414, 8418, 8423, 8428, 8433, 8439, 8443, 8450, 8455, 8459, 8462, 8467, 8472, 8478, 8483, 8488, 8492, 8495,
    8498, 8502, 8507, 8513, 8519, 8526, 8532, 8538, 8543, 8548, 8554, 8560, 8565, 8570, 8576, 8582, 8587, 8594, 8599, 8603,
    8608, 8613, 8618, 8623, 8628, 8634, 8642, 8645, 8652, 8658, 8663, 8669, 8677, 8683, 8687, 8690, 8693, 8696, 8700, 8706,
    8711, 8715, 8721, 8727, 8733, 8738, 8744, 8748, 8751, 8756, 8761, 8767, 8771, 8776, 8781, 8784, 8787, 8791, 8795, 8798,
    8803, 8807, 8813, 8818, 8823, 8828, 8833, 8838, 8841, 8844, 8848, 8853, 8858, 8863, 8867, 8873, 8878, 8884, 8890, 8897,
    8902, 8907, 8912, 8917, 8923, 8927, 8930, 8933, 8937, 8941, 8946, 8952, 8958, 8964, 8969, 8973, 8979, 8986, 8992, 8997,
    9002, 9006, 9011, 9016, 9021, 9026, 9031, 9038, 9043, 9048, 9052, 9055, 9059, 9064, 9069, 9073, 9078, 9084, 9089, 9095,
    9099, 9103, 9108, 9112, 9118, 9123, 9128, 9134, 9138, 9144, 9149, 9155, 9160, 9164, 9168, 9173, 9178, 9182, 9186, 9190,
    9193, 9197, 9203, 9208, 9212, 9218, 9223, 9228, 9234, 9238, 9245, 9250, 9253, 9257, 9261, 9266, 9271, 9279, 9285, 9289,
    9294, 9299, 9303, 9309, 9314, 9320, 9328, 9331, 9335, 9339, 9343, 9347, 9352, 9356, 9361, 9367, 9372, 9379, 9383, 9389,
    9395, 9400, 9404, 9407, 9410, 9416, 9421, 9426, 9433, 9441, 9450, 9458, 9465, 9470, 9475, 9482, 9488, 9493, 9497, 9502,
    9505, 9510, 9514, 9518, 9524, 9528, 9533, 9537, 9541, 9545, 9551, 9554, 9557, 9561, 9566, 9571, 9576, 9581, 9586, 9591,
    9597, 9602, 9608, 9612, 9618, 9624, 9630, 9635, 9642, 9648, 9654, 9659, 9665, 9672, 9677, 9682, 9688, 9694, 9699, 9705,
    9711, 9718, 9725, 9730, 9736, 9741, 9745, 9750, 9753, 9758, 9763, 9769, 9775, 9779, 9784, 9789, 9796, 9802, 9807, 9812,
    9816, 9821, 9827, 9832, 9836, 9841, 9847, 9853, 9859, 9864, 9870, 9875, 9879, 9884, 9888, 9894, 9898, 9903, 9906, 9909,
    9914, 9919, 9924, 9928, 9934, 9939, 9945, 9949, 9956, 9961, 9966, 9971, 9976, 9982, 9987, 9995, 10002, 10006, 10009, 10012,
    10017, 10021, 10027, 10030, 10035, 10038, 10044, 10050, 10055, 10059, 10064, 10070, 10075, 10080, 10085, 10090, 10095, 10100, 10105, 10111,
    10118, 10124, 10130, 10135, 10140, 10146, 10151, 10156, 10162, 10168, 10171, 10174, 10179, 10186, 10192, 10195, 10199, 10204, 10209, 10215,
    10220, 10224, 10228, 10234, 10240, 10245, 10250, 10254, 10257, 10263, 10268, 10273, 10281, 10285, 10288, 10292, 10296, 10303, 10307, 10312,
    10317, 10322, 10327, 10332, 10338, 10345, 10350, 10354, 10360, 10366, 10371, 10376, 10382, 10388, 10392, 10397, 10401, 10405, 10410, 10415,
    10420, 10425, 10430, 10436, 10442, 10447, 10452, 10457, 10462, 10466, 10470, 10475, 10481, 10486, 10492, 10497, 10502, 10506, 10511, 10516,
    10521, 10526, 10532, 10538, 10543, 10548, 10553, 10556, 10561, 10567, 10571, 10577, 10582, 10586, 10592, 10598, 10603, 10608, 10614, 10620,
    10625, 10630, 10635, 10640, 10645, 10649, 10653, 10658, 10661, 10666, 10671, 10676, 10681, 10686, 10691, 10697, 10702, 10707, 10712, 10716,
    10720, 10727, 10733, 10739, 10744, 10750, 10755, 10759, 10763, 10766, 10771, 10777, 10783, 10787, 10792, 10797, 10802, 10807, 10811, 10817,
    10825, 10829, 10834, 10839, 10844, 10849, 10854, 10857, 10860, 10863, 10867, 10870, 10874, 10879, 10885, 10890, 10896, 10901, 10906, 10911,
    10915, 10919, 10924, 10929, 10934, 10940, 10945, 10950, 10956, 10960, 10965, 10970, 10975, 10979, 10984, 10990, 10996, 11004, 11009, 11013,
    11019, 11025, 11031, 11036, 11041, 11046, 11053, 11057, 11063, 11067, 11072, 11077, 11083, 11089, 11093, 11101, 11105, 11110, 11117, 11123,
    11130, 11135, 11140, 11146, 11151, 11159, 11164, 11169, 11173, 11178, 11183, 11188, 11193, 11200, 11209, 11216, 11222, 11226, 11230, 11234,
    11238, 11243, 11248, 11253, 11257, 11262, 11269, 11274, 11279, 11283, 11288, 11292, 11295, 11299, 11304, 11309, 11313, 11317, 11323, 11330,
    11335, 11340, 11345, 11349, 11354, 11359, 11364, 11369, 11372, 11376, 11383, 11387, 11392, 11397, 11402, 11407, 11413, 11419, 11425, 11428,
    11432, 11437, 11442, 11447, 11453, 11458, 11463, 11468, 11473, 11477, 11483, 11488, 11494, 11499, 11505, 11510, 11516, 11524, 11529, 11533,
    11538, 11543, 11548, 11552, 11557, 11561, 11566, 11570, 11576, 11582, 11588, 11593, 11598, 11603, 11610, 11614, 11620, 11625, 11629, 11636,
    11640, 11645, 11650, 11654, 11660, 11666, 11672, 11678, 11683, 11688, 11694, 11701, 11705, 11710, 11715, 11721, 11726, 11731, 11736, 11741,
    11745, 11751, 11757, 11762, 11770, 11774, 11780, 11784, 11789, 11794, 11800, 11806, 11811, 11817, 11822, 11831, 11836, 11841, 11847, 11852,
    11857, 11863, 11868, 11872, 11878, 11883, 11888, 11893, 11896, 11899, 11905, 11911, 11917, 11924, 11930, 11935, 11940, 11946, 11952, 11957,
    11960, 11964, 11967, 11972, 11976, 11982, 11985, 11990, 11996, 12000, 12008, 12011, 12017, 12022, 12029, 12035, 12039, 12043, 12048, 12052,
    12056, 12060, 12065, 12069, 12072, 12076, 12080, 12084, 12089, 12096, 12101, 12105, 12109, 12115, 12120, 12123, 12126, 12129, 12133, 12137,
    12142, 12147, 12151, 12156, 12162, 12169, 12177, 12182, 12186, 12190, 12195, 12198, 12201, 12206, 12210, 12213, 12218, 12222, 12226, 12231,
    12235, 12242, 12247, 12250, 12254, 12259, 12263, 12267, 12270, 12273, 12279, 12284, 12289, 12295, 12300, 12305, 12310, 12318, 12323, 12328,
    12334, 12340, 12346, 12353, 12358, 12363, 12368, 12374, 12381, 12387, 12391, 12396, 12399, 12404, 12407, 12411, 12417, 12424, 12428, 12431,
    12435, 12441, 12447, 12451, 12456, 12460, 12465, 12469, 12477, 12483, 12488, 12495, 12500, 12505, 12514, 12519, 12525, 12533, 12538, 12543,
    12549, 12556, 12561, 12566, 12571, 12577, 12582, 12587, 12592, 12597, 12601, 12607, 12612, 12617, 12623, 12628, 12635, 12642, 12649, 12653,
    12658, 12661, 12665, 12668, 12672, 12677, 12682, 12686, 12689, 12694, 12699, 12706, 12711, 12717, 12722, 12728, 12734, 12739, 12745, 12750,
    12755, 12760, 12766, 12771, 12777, 12782, 12787, 12794, 12799, 12805, 12809, 12813, 12819, 12824, 12829, 12834, 12839, 12844, 12850, 12855,
    12862, 12867, 12871, 12876, 12880, 12885, 12892, 12899, 12906, 12912, 12916, 12919, 12923, 12928, 12933, 12936, 12941, 12945, 12950, 12955,
    12960, 12965, 12970, 12975, 12980, 12984, 12989, 12995, 13001, 13005, 13012, 13018, 13023, 13026, 13031, 13037, 13042, 13047, 13055, 13058,
    13062, 13070, 13078, 13084, 13091, 13096, 13100, 13107, 13113, 13118, 13123, 13129, 13134, 13139, 13145, 13150, 13156, 13161, 13166, 13171,
    13178, 13184, 13190, 13195, 13201, 13210, 13218, 13223, 13228, 13234, 13239, 13243, 13246, 13251, 13256, 13261, 13267, 13272, 13276, 13280,
    13283, 13286, 13290, 13294, 13299, 13302, 13307, 13313, 13317, 13322, 13327, 13333, 13338, 13345, 13352, 13357, 13361, 13365, 13371, 13375,
    13380, 13385, 13390, 13395, 13400, 13404, 13409, 13414, 13422, 13427, 13433, 13440, 13447, 13451, 13456, 13460, 13465, 13471, 13475, 13480,
    13485, 13488, 13491, 13495, 13499, 13506, 13511, 13516, 13520, 13525, 13532, 13535, 13539, 13544, 13552, 13557, 13563, 13568, 13572, 13577,
    13583, 13588, 13594, 13600, 13606, 13612, 13617, 13623, 13629, 13635, 13642, 13646, 13653, 13660, 13668, 13674, 13680, 13685, 13691, 13696,
    13701, 13706, 13711, 13717, 13724, 13727, 13732, 13737, 13743, 13748, 13751, 13755, 13759, 13764, 13770, 13776, 13784, 13790, 13797, 13802,
    13806, 13813, 13819, 13824, 13828, 13833, 13837, 13842, 13847, 13853, 13859, 13864, 13869, 13874, 13880, 13885, 13891, 13896, 13902, 13906,
    13912, 13917, 13923, 13928, 13934, 13939, 13946, 13953, 13959, 13963, 13966, 13969, 13973, 13977, 13982, 13988, 13993, 13997, 14003, 14009,
    14016, 14021, 14028, 14036, 14041, 14046, 14052, 14058, 14065, 14072, 14079, 14084, 14089, 14097, 14106, 14111, 14115, 14122, 14127, 14133,
    14140, 14145, 14149, 14152, 14155, 14158, 14163, 14169, 14172, 14176, 14181, 14185, 14188, 14193, 14200, 14209, 14213, 14220, 14225, 14230,
    14236, 14240, 14246, 14253, 14258, 14264, 14271, 14277, 14283, 14290, 14297, 14303, 14310, 14317, 14323, 14329, 14335, 14339, 14345, 14353,
    14357, 14363, 14368, 14373, 14380, 14385, 14390, 14397, 14401, 14404, 14410, 14417, 14423, 14428, 14434, 14438, 14443, 14449, 14454, 14459,
    14464, 14470, 14478, 14484, 14489, 14494, 14498, 14501, 14504, 14508, 14515, 14519, 14522, 14525, 14531, 14537, 14542, 14548, 14555, 14559,
    14563, 14567, 14571, 14574, 14577, 14581, 14586, 14591, 14596, 14601, 14608, 14612, 14617, 14623, 14628, 14634, 14638, 14644, 14650, 14655,
    14660, 14668, 14673, 14678, 14683, 14688, 14692, 14697, 14703, 14707, 14713, 14721, 14725, 14731, 14736, 14741, 14744, 14749, 14755, 14760,
    14766, 14773, 14779, 14785, 14790, 14795, 14802, 14806, 14810, 14815, 14820, 14824, 14831, 14836, 14843, 14848, 14853, 14858, 14864, 14869,
    14874, 14879, 14883, 14888, 14892, 14899, 14904, 14908, 14913, 14917, 14923, 14931, 14936, 14941, 14947, 14952, 14957, 14961, 14966, 14970,
    14975, 14979, 14983, 14989, 14995, 15000, 15005, 15010, 15014, 15019, 15025, 15030, 15035, 15040, 15046, 15050, 15055, 15059, 15062, 15071,
    15076, 15081, 15087, 15094, 15100, 15106, 15111, 15116, 15121, 15126, 15131, 15136, 15141, 15146, 15152, 15156, 15161, 15165, 15168, 15172,
    15177, 15181, 15185, 15190, 15196, 15201, 15206, 15211, 15215, 15220, 15225, 15230, 15238, 15243, 15247, 15251, 15257, 15261, 15267, 15272,
    15276, 15281, 15287, 15291, 15296, 15302, 15308, 15313, 15316, 15319, 15325, 15330, 15335, 15339, 15345, 15348, 15354, 15357, 15361, 15366,
    15369, 15373, 15379, 15384, 15389, 15395, 15403, 15408, 15412, 15416, 15420, 15425, 15431, 15436, 15444, 15450, 15457, 15463, 15470, 15476,
    15481, 15486, 15490, 15496, 15502, 15507, 15514, 15520, 15524, 15531, 15539, 15545, 15552, 15557, 15562, 15567, 15572, 15576, 15581, 15587,
    15594, 15599, 15602, 15606, 15612, 15615, 15618, 15622, 15625, 15630, 15636, 15640, 15647, 15651, 15656, 15661, 15666, 15670, 15674, 15680,
    15685, 15690, 15695, 15702, 15705, 15710, 15716, 15720, 15725, 15731, 15736, 15742, 15748, 15753, 15759, 15767, 15771, 15777, 15781, 15785,
    15790, 15796, 15801, 15806, 15810, 15815, 15820, 15824, 15828, 15833, 15836, 15839, 15843, 15847, 15850, 15854, 15857, 15861, 15867, 15872,
    15877, 15883, 15888, 15893, 15898, 15903, 15907, 15912, 15918, 15924, 15928, 15933, 15937, 15941, 15947, 15953, 15958, 15963, 15969, 15974,
    15980, 15988, 15993, 15999, 16006, 16012, 16018, 16024, 16031, 16036, 16041, 16048, 16052, 16057, 16064, 16071, 16075, 16080, 16084, 16090,
    16095, 16099, 16103, 16109, 16115, 16120, 16127, 16133, 16137, 16143, 16148, 16153, 16158, 16163, 16168, 16172, 16177, 16183, 16188, 16193,
    16197, 16202, 16205, 16210, 16213, 16219, 16225, 16230, 16234, 16240, 16245, 16252, 16257, 16263, 16270, 16276, 16279, 16282, 16287, 16293,
    16298, 16302, 16307, 16313, 16317, 16322, 16327, 16333, 16341, 16346, 16351, 16355, 16360, 16366, 16372, 16377, 16385, 16390, 16396, 16401,
    16407, 16413, 16419, 16426, 16431, 16437, 16444, 16451, 16456, 16461, 16465, 16470, 16476, 16481, 16486, 16490, 16494, 16499, 16504, 16511,
    16516, 16521, 16525, 16532, 16538, 16545, 16550, 16557, 16562, 16568, 16574, 16579, 16584, 16590, 16597, 16603, 16608, 16613, 16619, 16622,
    16626, 16631, 16636, 16640, 16646, 16650, 16655, 16660, 16666, 16671, 16676, 16681, 16686, 16691, 16697, 16703, 16710, 16716, 16723, 16730,
    16735, 16739, 16745, 16753, 16758, 16764, 16769, 16775, 16780, 16785, 16788, 16793, 16798, 16802, 16807, 16812, 16817, 16821, 16826, 16831,
    16837, 16844, 16848, 16854, 16860, 16866, 16873, 16878, 16883, 16889, 16893, 16897, 16901, 16904, 16908, 16913, 16918, 16925, 16932, 16937,
    16943, 16948, 16954, 16961, 16966, 16971, 16976, 16982, 16987, 16992, 16997, 17003, 17008, 17013, 17018, 17023, 17029, 17034, 17039, 17044,
    17050, 17055, 17061, 17070, 17077, 17083, 17092, 17097, 17102, 17106, 17111, 17115, 17120, 17126, 17133, 17138, 17143, 17150, 17156, 17162,
    17167, 17171, 17175, 17180, 17187, 17192, 17197, 17204, 17210, 17216, 17221, 17228, 17234, 17240, 17245, 17253, 17261, 17265, 17272, 17279,
    17284, 17290, 17296, 17302, 17307, 17312, 17317, 17320, 17327, 17332, 17338, 17343, 17347, 17354, 17359, 17363, 17368, 17375, 17381, 17387,
    17394, 17399, 17405, 17410, 17416, 17421, 17427, 17431, 17436, 17440, 17447, 17451, 17454, 17459, 17464, 17469, 17473, 17479, 17485, 17489,
    17494, 17498, 17505, 17510, 17516, 17522, 17527, 17531, 17536, 17543, 17550, 17558, 17565, 17571, 17578, 17584, 17589, 17595, 17600, 17606,
    17611, 17617, 17624, 17630, 17637, 17641, 17645, 17648, 17651, 17655, 17659, 17665, 17669, 17673, 17678, 17683, 17689, 17695, 17699, 17704,
    17709, 17713, 17718, 17724, 17730, 17735, 17741, 17746, 17752, 17759, 17765, 17770, 17777, 17784, 17790, 17795, 17800, 17806, 17811, 17816,
    17820, 17826, 17832, 17835, 17838, 17842, 17848, 17854, 17859, 17865, 17870, 17875, 17883, 17892, 17898, 17905, 17910, 17914, 17919, 17925,
    17931, 17936, 17940, 17944, 17948, 17952, 17956, 17961, 17967, 17973, 17978, 17982, 17987, 17992, 17997, 18002, 18007, 18014, 18019, 18023,
    18028, 18033, 18038, 18042, 18047, 18052, 18058, 18063, 18066, 18069, 18074, 18080, 18085, 18092, 18098, 18103, 18108, 18115, 18120, 18125,
    18131, 18134, 18137, 18141, 18146, 18151, 18156, 18160, 18165, 18172, 18178, 18184, 18190, 18200, 18206, 18211, 18215, 18219, 18224, 18228,
    18233, 18237, 18242, 18247, 18250, 18256, 18260, 18265, 18270, 18275, 18280, 18285, 18293, 18296, 18302, 18308, 18314, 18320, 18327, 18333,
    18340, 18347, 18352, 18358, 18366, 18372, 18379, 18384, 18389, 18395, 18400, 18406, 18412, 18420, 18429, 18436, 18442, 18447, 18453, 18458,
    18461, 18465, 18471, 18479, 18487, 18491, 18496, 18499, 18504, 18508, 18512, 18517, 18524, 18530, 18535, 18539, 18543, 18550, 18554, 18558,
    18563, 18567, 18571, 18576, 18580, 18584, 18589, 18595, 18600, 18607, 18614, 18617, 18620, 18628, 18634, 18641, 18648, 18653, 18658, 18663,
    18668, 18677, 18684, 18688, 18693, 18701, 18709, 18715, 18721, 18726, 18732, 18739, 18745, 18750, 18754, 18759, 18765, 18771, 18780, 18785,
    18791, 18797, 18803, 18810, 18815, 18820, 18825, 18831, 18837, 18842, 18847, 18853, 18859, 18863, 18870, 18875, 18881, 18887, 18893, 18896,
    18901, 18906, 18910, 18915, 18920, 18926, 18933, 18940, 18944, 18947, 18951, 18956, 18961, 18967, 18972, 18977, 18982, 18987, 18991, 18996,
    19002, 19007, 19013, 19017, 19022, 19027, 19033, 19038, 19043, 19050, 19057, 19064, 19072, 19078, 19086, 19090, 19095, 19101, 19107, 19113,
    19118, 19125, 19130, 19135, 19141, 19146, 19151, 19156, 19162, 19168, 19172, 19178, 19185, 19190, 19194, 19199, 19204, 19209, 19214, 19220,
    19227, 19232, 19239, 19245, 19251, 19257, 19263, 19271, 19275, 19281, 19285, 19291, 19297, 19304, 19311, 19318, 19324, 19331, 19336, 19339,
    19343, 19347, 19350, 19355, 19360, 19365, 19370, 19376, 19381, 19386, 19390, 19395, 19399, 19402, 19406, 19411, 19415, 19420, 19425, 19431,
    19435, 19440, 19446, 19451, 19456, 19462, 19468, 19473, 19479, 19485, 19491, 19496, 19501, 19507, 19512, 19518, 19524, 19529, 19534, 19540,
    19545, 19551, 19556, 19561, 19566, 19570, 19575, 19581, 19586, 19593, 19597, 19601, 19606, 19611, 19616, 19621, 19627, 19634, 19641, 19648,
    19654, 19659, 19665, 19671, 19677, 19682, 19688, 19691, 19695, 19700, 19705, 19710, 19716, 19722, 19727, 19730, 19734, 19739, 19745, 19750,
    19755, 19761, 19765, 19768, 19773, 19777, 19782, 19788, 19794, 19801, 19806, 19811, 19815, 19820, 19825, 19830, 19835, 19840, 19845, 19850,
    19855, 19860, 19864, 19869, 19873, 19878, 19884, 19888, 19893, 19897, 19904, 19912, 19917, 19923, 19927, 19933, 19939, 19946, 19953, 19958,
    19963, 19969, 19974, 19980, 19986, 19991, 19996, 20002, 20008, 20013, 20018, 20025, 20030, 20036, 20043, 20048, 20053, 20059, 20063, 20066,
    20073, 20078, 20084, 20090, 20095, 20099, 20105, 20109, 20112, 20117, 20122, 20127, 20133, 20138, 20143, 20148, 20153, 20158, 20161, 20165,
    20171, 20175, 20179, 20184, 20190, 20196, 20204, 20209, 20214, 20218, 20223, 20227, 20232, 20237, 20241, 20245, 20251, 20256, 20261, 20267,
    20272, 20278, 20282, 20287, 20292, 20298, 20303, 20309, 20315, 20321, 20327, 20332, 20338, 20343, 20350, 20355, 20359, 20364, 20369, 20374,
    20379, 20385, 20389, 20394, 20400, 20405, 20411, 20415, 20421, 20427, 20432, 20438, 20444, 20449, 20454, 20459, 20463, 20466, 20470, 20476,
    20480, 20485, 20488, 20492, 20497, 20503, 20508, 20512, 20518, 20525, 20532, 20537, 20542, 20547, 20552, 20558, 20562, 20570, 20575, 20581,
    20586, 20590, 20596, 20600, 20603, 20609, 20614, 20621, 20628, 20633, 20638, 20643, 20648, 20652, 20659, 20664, 20674, 20679, 20685, 20692,
    20697, 20702, 20707, 20713, 20720, 20729, 20733, 20739, 20742, 20746, 20750, 20755, 20760, 20765, 20770, 20774, 20779, 20784, 20788, 20793,
    20797, 20802, 20807, 20812, 20819, 20824, 20831, 20839, 20845, 20851, 20856, 20861, 20867, 20872, 20878, 20884, 20890, 20895, 20903, 20908,
    20913, 20918, 20924, 20928, 20933, 20938, 20943, 20947, 20953, 20958, 20962, 20965, 20968, 20971, 20975, 20980, 20985, 20990, 20995, 21000,
    21005, 21009, 21016, 21022, 21027, 21032, 21038, 21042, 21046, 21051, 21057, 21064, 21070, 21077, 21084, 21089, 21095, 21101, 21106, 21111,
    21115, 21120, 21126, 21131, 21135, 21140, 21146, 21150, 21155, 21163, 21167, 21173, 21177, 21181, 21185, 21188, 21192, 21197, 21203, 21209,
    21215, 21218, 21223, 21227, 21233, 21242, 21245, 21249, 21255, 21260, 21265, 21270, 21276, 21282, 21285, 21288, 21293, 21298, 21305, 21311,
    21316, 21322, 21329, 21334, 21338, 21343, 21348, 21353, 21357, 21364, 21371, 21377, 21382, 21387, 21391, 21396, 21400, 21406, 21413, 21418,
    21423, 21430, 21436, 21441, 21446, 21451, 21457, 21464, 21469, 21476, 21481, 21487, 21492, 21497, 21505, 21510, 21515, 21523, 21531, 21536,
    21543, 21550, 21553, 21556, 21560, 21567, 21572, 21576, 21581, 21587, 21593, 21598, 21604, 21608, 21615, 21620, 21626, 21634, 21640, 21644,
    21649, 21653, 21660, 21664, 21670, 21674, 21678, 21683, 21688, 21694, 21700, 21706, 21715, 21720, 21728, 21734, 21740, 21744, 21749, 21756,
    21762, 21767, 21773, 21778, 21782, 21787, 21792, 21797, 21803, 21808, 21813, 21819, 21824, 21829, 21835, 21841, 21847, 21852, 21859, 21865,
    21869, 21875, 21883, 21890, 21896, 21904, 21910, 21915, 21921, 21928, 21932, 21936, 21943, 21946, 21951, 21955, 21959, 21964, 21969, 21973,
    21977, 21982, 21986, 21993, 21997, 22000, 22003, 22009, 22013, 22016, 22020, 22024, 22029, 22034, 22040, 22046, 22049, 22053, 22057, 22064,
    22073, 22081, 22086, 22091, 22095, 22102, 22108, 22112, 22117, 22123, 22128, 22133, 22139, 22146, 22152, 22158, 22163, 22169, 22175, 22182,
    22188, 22193, 22198, 22203, 22208, 22214, 22220, 22225, 22230, 22236, 22242, 22247, 22251, 22255, 22261, 22264, 22267, 22271, 22276, 22282,
    22289, 22293, 22296, 22300, 22309, 22315, 22320, 22325, 22330, 22334, 22339, 22343, 22347, 22353, 22358, 22363, 22369, 22376, 22381, 22386,
    22391, 22397, 22403, 22408, 22413, 22419, 22424, 22431, 22435, 22439, 22444, 22449, 22454, 22461, 22466, 22473, 22480, 22486, 22493, 22500,
    22505, 22510, 22515, 22521, 22528, 22534, 22538, 22542, 22546, 22549, 22552, 22556, 22563, 22568, 22572, 22577, 22583, 22589, 22595, 22600,
    22606, 22612, 22617, 22623, 22627, 22633, 22640, 22647, 22655, 22662, 22667, 22674, 22681, 22685, 22692, 22698, 22702, 22707, 22713, 22719,
    22723, 22728, 22733, 22738, 22743, 22748, 22752, 22757, 22761, 22766, 22773, 22777, 22782, 22788, 22792, 22797, 22803, 22808, 22814, 22818,
    22823, 22830, 22834, 22839, 22844, 22849, 22854, 22859, 22867, 22872, 22876, 22880, 22885, 22889, 22894, 22900, 22903, 22907, 22912, 22917,
    22922, 22928, 22934, 22940, 22944, 22947, 22951, 22954, 22957, 22961, 22968, 22973, 22977, 22982, 22987, 22994, 22999, 23004, 23009, 23016,
    23022, 23027, 23032, 23037, 23043, 23049, 23053, 23059, 23064, 23069, 23075, 23078, 23082, 23087, 23093, 23099, 23106, 23113, 23121, 23126,
    23133, 23141, 23151, 23158, 23166, 23171, 23176, 23182, 23187, 23194, 23201, 23208, 23214, 23219, 23224, 23232, 23239, 23247, 23253, 23259,
    23265, 23272, 23279, 23288, 23297, 23306, 23312, 23317, 23322, 23327, 23330, 23334, 23341, 23345, 23349, 23352, 23357, 23361, 23369, 23373,
    23376, 23380, 23385, 23390, 23394, 23398, 23402, 23406, 23410, 23416, 23421, 23426, 23431, 23437, 23444, 23449, 23454, 23458, 23461, 23464,
    23468, 23472, 23477, 23481, 23484, 23488, 23492, 23498, 23503, 23508, 23511, 23515, 23519, 23523, 23531, 23539, 23544, 23549, 23556, 23561,
    23566, 23571, 23575, 23579, 23583, 23588, 23593, 23599, 23603, 23609, 23615, 23619, 23624, 23629, 23634, 23638, 23642, 23646, 23650, 23655,
    23661, 23666, 23671, 23677, 23683, 23688, 23693, 23698, 23704, 23709, 23713, 23718, 23723, 23728, 23734, 23740, 23744, 23748, 23754, 23759,
    23762, 23768, 23772, 23777, 23782, 23787, 23791, 23798, 23801, 23804, 23808, 23813, 23819, 23824, 23830, 23836, 23840, 23845, 23850, 23854,
    23859, 23865, 23871, 23877, 23883, 23889, 23893, 23897, 23901, 23907, 23912, 23918, 23923, 23927, 23932, 23936, 23941, 23946, 23952, 23956,
    23962, 23968, 23972, 23977, 23981, 23986, 23990, 23995, 24000, 24006, 24011, 24015, 24020, 24026, 24033, 24039, 24043, 24047, 24051, 24056,
    24060, 24064, 24068, 24074, 24079, 24085, 24089, 24093, 24100, 24106, 24111, 24115, 24120, 24125, 24131, 24136, 24141, 24145, 24149, 24155,
    24161, 24166, 24173, 24178, 24185, 24190, 24195, 24200, 24205, 24209, 24217, 24222, 24227, 24232, 24237, 24243, 24249, 24255, 24260, 24266,
    24270, 24273, 24278, 24283, 24288, 24292, 24297, 24302, 24308, 24313, 24318, 24324, 24328, 24333, 24340, 24345, 24349, 24354, 24359, 24364,
    24369, 24374, 24379, 24385, 24389, 24396, 24402, 24408, 24415, 24420, 24425, 24429, 24434, 24439, 24448, 24453, 24458, 24462, 24465, 24469,
    24472, 24476, 24480, 24483, 24487, 24491, 24497, 24502, 24506, 24511, 24518, 24524, 24529, 24534, 24539, 24545, 24550, 24557, 24561, 24567,
    24574, 24578, 24582, 24586, 24591, 24597, 24602, 24607, 24613, 24619, 24624, 24630, 24637, 24643, 24651, 24657, 24664, 24670, 24676, 24683,
    24689, 24695, 24699, 24704, 24710, 24718, 24723, 24729, 24734, 24740, 24745, 24751, 24757, 24763, 24769, 24774, 24778, 24783, 24787, 24791,
    24797, 24803, 24807, 24812, 24817, 24822, 24827, 24832, 24837, 24842, 24846, 24851, 24857, 24862, 24867, 24872, 24877, 24882, 24886, 24894,
    24898, 24902, 24909, 24916, 24920, 24925, 24930, 24935, 24940, 24945, 24951, 24956, 24961, 24966, 24972, 24975, 24978, 24983, 24987, 24993,
    24998, 25003, 25008, 25012, 25017, 25022, 25030, 25035, 25041, 25047, 25050, 25053, 25060, 25066, 25071, 25077, 25082, 25087, 25092, 25096,
    25101, 25105, 25109, 25112, 25119, 25123, 25130, 25136, 25141, 25145, 25150, 25155, 25159, 25162, 25167, 25172, 25176, 25179, 25185, 25189,
    25195, 25198, 25202, 25210, 25214, 25220, 25228, 25232, 25236, 25241, 25245, 25248, 25251, 25254, 25259, 25264, 25268, 25272, 25276, 25279,
    25282, 25289, 25293, 25298, 25303, 25306, 25310, 25315, 25319, 25322, 25326, 25330, 25335, 25340, 25346, 25349, 25353, 25356, 25362, 25369,
    25374, 25377, 25383, 25386, 25390, 25393, 25397, 25400, 25404, 25408, 25412, 25415, 25418, 25422, 25427, 25431, 25436, 25442, 25447, 25452,
    25455, 25459, 25463, 25468, 25472, 25480, 25484, 25488, 25491, 25494, 25498, 25505, 25509, 25513, 25519, 25523, 25530, 25535, 25541, 25545,
    25549, 25554, 25558, 25561, 25565, 25569, 25573, 25579, 25584, 25588, 25593, 25598, 25602, 25606, 25611, 25616, 25620, 25625, 25630, 25634,
    25638, 25642, 25646, 25652, 25657, 25662, 25666, 25670, 25674, 25679, 25685, 25690, 25696, 25701, 25704, 25707, 25711, 25715, 25718, 25721,
    25725, 25729, 25733, 25738, 25742, 25745, 25751, 25755, 25759, 25762, 25765, 25768, 25772, 25777, 25783, 25787, 25790, 25794,
};

static const uint8_t g_en_us_pattern_lengths[] = {
    4, 6, 4, 4, 5, 4, 4, 5, 4, 5, 6, 4, 6, 5, 4, 4, 4, 6, 5, 4,
    4, 4, 4, 5, 5, 4, 5, 5, 6, 4, 6, 6, 5, 6, 6, 4, 5, 3, 7, 3,
    5, 4, 6, 4, 7, 6, 4, 5, 5, 7, 5, 7, 4, 5, 4, 5, 6, 5, 3, 4,
    3, 4, 5, 5, 4, 4, 6, 5, 3, 6, 3, 6, 3, 8, 4, 4, 7, 3, 6, 3,
    5, 5, 6, 5, 4, 4, 4, 6, 5, 3, 5, 5, 6, 6, 4, 4, 4, 5, 6, 5,
    4, 4, 5, 4, 6, 3, 5, 4, 4, 4, 5, 4, 4, 4, 6, 6, 5, 4, 6, 5,
    3, 5, 7, 4, 4, 4, 4, 5, 4, 4, 4, 5, 5, 5, 6, 3, 6, 5, 7, 6,
    6, 4, 7, 6, 5, 5, 5, 6, 5, 4, 6, 6, 3, 4, 5, 6, 4, 4, 4, 3,
    5, 4, 4, 6, 5, 6, 4, 5, 4, 6, 7, 6, 5, 4, 5, 8, 7, 4, 5, 6,
    4, 7, 4, 5, 4, 5, 5, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 4, 6,
    3, 3, 5, 6, 6, 3, 6, 3, 3, 8, 3, 7, 6, 3, 3, 4, 5, 5, 5, 5,
    4, 5, 6, 4, 5, 7, 4, 6, 5, 6, 4, 6, 4, 4, 4, 3, 4, 4, 6, 5,
    6, 5, 5, 3, 3, 4, 4, 3, 5, 4, 6, 5, 6, 6, 3, 5, 4, 5, 4, 5,
    5, 5, 4, 5, 3, 4, 4, 4, 5, 5, 4, 4, 2, 5, 5, 3, 4, 5, 5, 4,
    4, 4, 4, 4, 5, 4, 3, 4, 4, 3, 5, 2, 3, 6, 4, 4, 5, 4, 4, 3,
    3, 3, 3, 4, 5, 5, 4, 3, 3, 3, 3, 3, 2, 3, 4, 4, 3, 5, 4, 5,
    2, 4, 4, 4, 4, 4, 3, 5, 6, 4, 3, 5, 4, 5, 5, 3, 5, 4, 4, 5,
    4, 4, 3, 4, 4, 5, 5, 6, 5, 7, 5, 4, 4, 5, 4, 5, 3, 4, 5, 5,
    2, 5, 5, 6, 4, 5, 5, 5, 3, 5, 5, 4, 5, 4, 4, 6, 4, 3, 5, 4,
    4, 5, 4, 5, 5, 5, 4, 4, 5, 4, 4, 5, 5, 3, 5, 4, 5, 4, 5, 5,
    4, 4, 5, 4, 5, 4, 5, 5, 5, 5, 6, 4, 4, 4, 4, 4, 4, 2, 4, 4,
    7, 5, 5, 4, 6, 7, 4, 5, 5, 3, 4, 5, 5, 5, 5, 3, 4, 2, 5, 5,
    6, 4, 7, 5, 4, 4, 6, 6, 4, 4, 4, 5, 6, 6, 6, 4, 5, 4, 5, 5,
    4, 4, 3, 5, 5, 5, 4, 6, 4, 4, 4, 4, 5, 4, 3, 3, 7, 4, 4, 4,
    3, 4, 5, 4, 5, 4, 4, 4, 4, 3, 4, 4, 4, 5, 4, 5, 8, 3, 5, 4,
    5, 4, 4, 5, 5, 5, 5, 5, 6, 5, 4, 3, 5, 5, 7, 4, 5, 4, 4, 4,
    4, 4, 7, 5, 4, 4, 6, 4, 4, 3, 5, 4, 5, 4, 6, 4, 3, 4, 4, 4,
    5, 3, 3, 4, 6, 4, 3, 5, 4, 3, 5, 5, 4, 3, 4, 4, 5, 5, 5, 5,
    3, 5, 4, 5, 4, 4, 4, 3, 4, 3, 4, 4, 4, 3, 3, 5, 4, 3, 7, 6,
    4, 4, 6, 4, 4, 5, 5, 7, 5, 3, 5, 3, 2, 3, 4, 5, 4, 2, 3, 4,
    4, 3, 4, 4, 4, 4, 4, 3, 4, 4, 3, 5, 4, 3, 4, 5, 3, 5, 4, 4,
    5, 3, 4, 2, 2, 3, 3, 5, 3, 4, 4, 3, 3, 5, 6, 5, 4, 5, 5, 4,
    5, 5, 4, 3, 5, 6, 4, 5, 4, 2, 2, 2, 5, 5, 4, 4, 5, 5, 4, 3,
    5, 5, 2, 2, 4, 3, 4, 3, 5, 5, 4, 5, 3, 4, 4, 4, 4, 4, 5, 3,
    4, 4, 4, 5, 5, 2, 4, 5, 5, 2, 4, 2, 3, 3, 3, 6, 4, 4, 4, 3,
    5, 4, 5, 6, 6, 5, 7, 4, 4, 6, 5, 4, 6, 2, 2, 3, 3, 2, 5, 4,
    4, 5, 5, 3, 3, 5, 5, 6, 4, 4, 4, 5, 5, 5, 5, 4, 5, 5, 6, 6,
    4, 5, 4, 5, 5, 2, 4, 4, 6, 4, 5, 3, 4, 5, 3, 4, 4, 3, 4, 4,
    4, 4, 3, 5, 4, 5, 6, 4, 3, 4, 3, 2, 3, 4, 6, 6, 3, 5, 4, 5,
    5, 5, 5, 5, 6, 4, 6, 7, 5, 4, 4, 4, 3, 4, 5, 4, 2, 3, 4, 4,
    3, 4, 6, 5, 3, 4, 4, 3, 3, 4, 5, 5, 4, 5, 4, 4, 4, 4, 4, 5,
    5, 5, 3, 5, 3, 2, 3, 2, 4, 8, 5, 5, 4, 4, 4, 3, 2, 2, 4, 3,
    3, 4, 3, 5, 4, 4, 5, 5, 4, 4, 4, 4, 4, 6, 5, 4, 4, 5, 4, 6,
    3, 4, 4, 4, 4, 2, 5, 5, 6, 5, 4, 5, 4, 3, 4, 4, 4, 5, 6, 6,
    6, 5, 5, 5, 4, 2, 2, 4, 5, 5, 3, 4, 5, 5, 7, 4, 3, 3, 3, 3,
    5, 4, 6, 5, 4, 4, 4, 3, 4, 4, 5, 7, 5, 3, 5, 3, 5, 5, 4, 2,
    3, 2, 3, 3, 4, 3, 3, 6, 3, 4, 4, 4, 4, 3, 5, 5, 4, 3, 4, 3,
    2, 2, 2, 4, 4, 3, 4, 6, 5, 5, 5, 5, 6, 7, 5, 3, 4, 8, 4, 5,
    5, 4, 3, 4, 5, 6, 5, 6, 5, 5, 3, 5, 4, 6, 4, 3, 4, 4, 4, 3,
    4, 4, 6, 4, 3, 4, 4, 5, 4, 5, 5, 4, 3, 5, 4, 3, 5, 3, 2, 3,
    4, 3, 3, 2, 3, 3, 4, 6, 5, 4, 4, 3, 4, 3, 6, 4, 6, 7, 3, 4,
    5, 4, 5, 3, 4, 4, 3, 4, 5, 5, 5, 3, 4, 4, 4, 3, 2, 2, 3, 4,
    5, 4, 5, 5, 4, 3, 3, 3, 2, 2, 2, 3, 4, 3, 3, 3, 4, 4, 5, 5,
    5, 4, 4, 4, 3, 3, 4, 3, 6, 3, 2, 2, 6, 4, 3, 5, 4, 6, 4, 6,
    4, 7, 6, 4, 4, 6, 3, 2, 3, 3, 3, 4, 3, 2, 3, 5, 3, 4, 5, 5,
    5, 4, 3, 4, 5, 3, 6, 3, 4, 2, 2, 2, 3, 4, 4, 3, 4, 3, 5, 4,
    5, 3, 5, 5, 5, 4, 6, 4, 4, 5, 5, 5, 4, 4, 5, 4, 4, 4, 3, 5,
    5, 5, 4, 3, 5, 4, 4, 2, 5, 5, 4, 4, 3, 4, 5, 4, 3, 6, 5, 3,
    4, 8, 6, 5, 4, 4, 5, 5, 5, 4, 5, 6, 5, 4, 5, 5, 3, 5, 5, 4,
    3, 4, 5, 3, 3, 4, 4, 4, 3, 4, 4, 5, 4, 4, 4, 3, 4, 4, 4, 4,
    7, 5, 3, 4, 3, 4, 4, 3, 4, 3, 3, 4, 4, 3, 2, 5, 3, 4, 5, 4,
    5, 7, 4, 6, 6, 4, 4, 4, 4, 5, 4, 3, 4, 4, 4, 4, 3, 2, 4, 2,
    3, 3, 3, 4, 4, 4, 4, 5, 4, 4, 4, 4, 2, 4, 5, 4, 4, 3, 4, 4,
    5, 6, 4, 5, 4, 6, 4, 4, 5, 4, 4, 4, 3, 3, 5, 5, 5, 5, 6, 4,
    5, 4, 4, 5, 6, 4, 4, 5, 4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4,
    5, 3, 3, 4, 4, 5, 4, 6, 4, 5, 5, 4, 5, 5, 4, 5, 4, 6, 4, 4,
    5, 4, 3, 5, 5, 6, 5, 5, 4, 4, 4, 5, 5, 5, 5, 4, 5, 4, 4, 4,
    5, 4, 4, 4, 3, 3, 4, 4, 4, 4, 6, 6, 4, 4, 4, 3, 3, 3, 6, 3,
    3, 5, 3, 4, 5, 3, 3, 4, 4, 3, 3, 4, 5, 4, 5, 8, 4, 7, 4, 3,
    5, 6, 5, 5, 4, 5, 4, 5, 4, 5, 2, 5, 5, 3, 4, 5, 4, 6, 3, 4,
    4, 5, 4, 5, 5, 5, 5, 5, 5, 6, 5, 5, 4, 5, 5, 4, 3, 3, 4, 6,
    5, 5, 5, 5, 4, 4, 4, 4, 5, 3, 5, 5, 5, 4, 3, 4, 4, 4, 4, 3,
    5, 5, 4, 4, 3, 4, 5, 3, 6, 6, 3, 4, 5, 4, 4, 3, 4, 5, 5, 6,
    6, 6, 3, 4, 5, 3, 4, 4, 6, 6, 4, 4, 6, 4, 5, 4, 4, 5, 4, 5,
    3, 6, 5, 6, 5, 3, 5, 5, 5, 5, 4, 5, 4, 5, 6, 4, 5, 3, 4, 5,
    4, 5, 8, 4, 5, 4, 5, 4, 6, 6, 3, 5, 4, 4, 5, 5, 5, 5, 4, 4,
    3, 2, 6, 3, 3, 4, 3, 4, 5, 4, 4, 4, 5, 4, 5, 5, 5, 5, 4, 5,
    3, 4, 4, 4, 4, 4, 3, 3, 4, 4, 3, 4, 5, 4, 3, 3, 4, 3, 2, 4,
    4, 4, 3, 4, 5, 4, 5, 3, 5, 4, 5, 4, 5, 2, 2, 3, 4, 5, 3, 6,
    4, 4, 3, 4, 4, 4, 5, 3, 6, 4, 3, 2, 4, 4, 5, 4, 4, 3, 2, 2,
    3, 4, 5, 5, 6, 5, 5, 4, 4, 5, 5, 4, 4, 5, 5, 4, 6, 4, 3, 4,
    4, 4, 4, 4, 5, 7, 2, 6, 5, 4, 5, 7, 5, 3, 2, 2, 2, 3, 5, 4,
    3, 5, 5, 5, 4, 5, 3, 2, 4, 4, 5, 3, 4, 4, 2, 2, 3, 3, 2, 4,
    3, 5, 4, 4, 4, 4, 4, 2, 2, 3, 4, 4, 4, 3, 5, 4, 5, 5, 6, 4,
    4, 4, 4, 5, 3, 2, 2, 3, 3, 4, 5, 5, 5, 4, 3, 5, 6, 5, 4, 4,
    3, 4, 4, 4, 4, 4, 6, 4, 4, 3, 2, 3, 4, 4, 3, 4, 5, 4, 5, 3,
    3, 4, 3, 5, 4, 4, 5, 3, 5, 4, 5, 4, 3, 3, 4, 4, 3, 3, 3, 2,
    3, 5, 4, 3, 5, 4, 4, 5, 3, 6, 4, 2, 3, 3, 4, 4, 7, 5, 3, 4,
    4, 3, 5, 4, 5, 7, 2, 3, 3, 3, 3, 4, 3, 4, 5, 4, 6, 3, 5, 5,
    4, 3, 2, 2, 5, 4, 4, 6, 7, 8, 7, 6, 4, 4, 6, 5, 4, 3, 4, 2,
    4, 3, 3, 5, 3, 4, 3, 3, 3, 5, 2, 2, 3, 4, 4, 4, 4, 4, 4, 5,
    4, 5, 3, 5, 5, 5, 4, 6, 5, 5, 4, 5, 6, 4, 4, 5, 5, 4, 5, 5,
    6, 6, 4, 5, 4, 3, 4, 2, 4, 4, 5, 5, 3, 4, 4, 6, 5, 4, 4, 3,
    4, 5, 4, 3, 4, 5, 5, 5, 4, 5, 4, 3, 4, 3, 5, 3, 4, 2, 2, 4,
    4, 4, 3, 5, 4, 5, 3, 6, 4, 4, 4, 4, 5, 4, 7, 6, 3, 2, 2, 4,
    3, 5, 2, 4, 2, 5, 5, 4, 3, 4, 5, 4, 4, 4, 4, 4, 4, 4, 5, 6,
    5, 5, 4, 4, 5, 4, 4, 5, 5, 2, 2, 4, 6, 5, 2, 3, 4, 4, 5, 4,
    3, 3, 5, 5, 4, 4, 3, 2, 5, 4, 4, 7, 3, 2, 3, 3, 6, 3, 4, 4,
    4, 4, 4, 5, 6, 4, 3, 5, 5, 4, 4, 5, 5, 3, 4, 3, 3, 4, 4, 4,
    4, 4, 5, 5, 4, 4, 4, 4, 3, 3, 4, 5, 4, 5, 4, 4, 3, 4, 4, 4,
    4, 5, 5, 4, 4, 4, 2, 4, 5, 3, 5, 4, 3, 5, 5, 4, 4, 5, 5, 4,
    4, 4, 4, 4, 3, 3, 4, 2, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 3, 3,
    6, 5, 5, 4, 5, 4, 3, 3, 2, 4, 5, 5, 3, 4, 4, 4, 4, 3, 5, 7,
    3, 4, 4, 4, 4, 4, 2, 2, 2, 3, 2, 3, 4, 5, 4, 5, 4, 4, 4, 3,
    3, 4, 4, 4, 5, 4, 4, 5, 3, 4, 4, 4, 3, 4, 5, 5, 7, 4, 3, 5,
    5, 5, 4, 4, 4, 6, 3, 5, 3, 4, 4, 5, 5, 3, 7, 3, 4, 6, 5, 6,
    4, 4, 5, 4, 7, 4, 4, 3, 4, 4, 4, 4, 6, 8, 6, 5, 3, 3, 3, 3,
    4, 4, 4, 3, 4, 6, 4, 4, 3, 4, 3, 2, 3, 4, 4, 3, 3, 5, 6, 4,
    4, 4, 3, 4, 4, 4, 4, 2, 3, 6, 3, 4, 4, 4, 4, 5, 5, 5, 2, 3,
    4, 4, 4, 5, 4, 4, 4, 4, 3, 5, 4, 5, 4, 5, 4, 5, 7, 4, 3, 4,
    4, 4, 3, 4, 3, 4, 3, 5, 5, 5, 4, 4, 4, 6, 3, 5, 4, 3, 6, 3,
    4, 4, 3, 5, 5, 5, 5, 4, 4, 5, 6, 3, 4, 4, 5, 4, 4, 4, 4, 3,
    5, 5, 4, 7, 3, 5, 3, 4, 4, 5, 5, 4, 5, 4, 8, 4, 4, 5, 4, 4,
    5, 4, 3, 5, 4, 4, 4, 2, 2, 5, 5, 5, 6, 5, 4, 4, 5, 5, 4, 2,
    3, 2, 4, 3, 5, 2, 4, 5, 3, 7, 2, 5, 4, 6, 5, 3, 3, 4, 3, 3,
    3, 4, 3, 2, 3, 3, 3, 4, 6, 4, 3, 3, 5, 4, 2, 2, 2, 3, 3, 4,
    4, 3, 4, 5, 6, 7, 4, 3, 3, 4, 2, 2, 4, 3, 2, 4, 3, 3, 4, 3,
    6, 4, 2, 3, 4, 3, 3, 2, 2, 5, 4, 4, 5, 4, 4, 4, 7, 4, 4, 5,
    5, 5, 6, 4, 4, 4, 5, 6, 5, 3, 4, 2, 4, 2, 3, 5, 6, 3, 2, 3,
    5, 5, 3, 4, 3, 4, 3, 7, 5, 4, 6, 4, 4, 8, 4, 5, 7, 4, 4, 5,
    6, 4, 4, 4, 5, 4, 4, 4, 4, 3, 5, 4, 4, 5, 4, 6, 6, 6, 3, 4,
    2, 3, 2, 3, 4, 4, 3, 2, 4, 4, 6, 4, 5, 4, 5, 5, 4, 5, 4, 4,
    4, 5, 4, 5, 4, 4, 6, 4, 5, 3, 3, 5, 4, 4, 4, 4, 4, 5, 4, 6,
    4, 3, 4, 3, 4, 6, 6, 6, 5, 3, 2, 3, 4, 4, 2, 4, 3, 4, 4, 4,
    4, 4, 4, 4, 3, 4, 5, 5, 3, 6, 5, 4, 2, 4, 5, 4, 4, 7, 2, 3,
    7, 7, 5, 6, 4, 3, 6, 5, 4, 4, 5, 4, 4, 5, 4, 5, 4, 4, 4, 6,
    5, 5, 4, 5, 8, 7, 4, 4, 5, 4, 3, 2, 4, 4, 4, 5, 4, 3, 3, 2,
    2, 3, 3, 4, 2, 4, 5, 3, 4, 4, 5, 4, 6, 6, 4, 3, 3, 5, 3, 4,
    4, 4, 4, 4, 3, 4, 4, 7, 4, 5, 6, 6, 3, 4, 3, 4, 5, 3, 4, 4,
    2, 2, 3, 3, 6, 4, 4, 3, 4, 6, 2, 3, 4, 7, 4, 5, 4, 3, 4, 5,
    4, 5, 5, 5, 5, 4, 5, 5, 5, 6, 3, 6, 6, 7, 5, 5, 4, 5, 4, 4,
    4, 4, 5, 6, 2, 4, 4, 5, 4, 2, 3, 3, 4, 5, 5, 7, 5, 6, 4, 3,
    6, 5, 4, 3, 4, 3, 4, 4, 5, 5, 4, 4, 4, 5, 4, 5, 4, 5, 3, 5,
    4, 5, 4, 5, 4, 6, 6, 5, 3, 2, 2, 3, 3, 4, 5, 4, 3, 5, 5, 6,
    4, 6, 7, 4, 4, 5, 5, 6, 6, 6, 4, 4, 7, 8, 4, 3, 6, 4, 5, 6,
    4, 3, 2, 2, 2, 4, 5, 2, 3, 4, 3, 2, 4, 6, 8, 3, 6, 4, 4, 5,
    3, 5, 6, 4, 5, 6, 5, 5, 6, 6, 5, 6, 6, 5, 5, 5, 3, 5, 7, 3,
    5, 4, 4, 6, 4, 4, 6, 3, 2, 5, 6, 5, 4, 5, 3, 4, 5, 4, 4, 4,
    5, 7, 5, 4, 4, 3, 2, 2, 3, 6, 3, 2, 2, 5, 5, 4, 5, 6, 3, 3,
    3, 3, 2, 2, 3, 4, 4, 4, 4, 6, 3, 4, 5, 4, 5, 3, 5, 5, 4, 4,
    7, 4, 4, 4, 4, 3, 4, 5, 3, 5, 7, 3, 5, 4, 4, 2, 4, 5, 4, 5,
    6, 5, 5, 4, 4, 6, 3, 3, 4, 4, 3, 6, 4, 6, 4, 4, 4, 5, 4, 4,
    4, 3, 4, 3, 6, 4, 3, 4, 3, 5, 7, 4, 4, 5, 4, 4, 3, 4, 3, 4,
    3, 3, 5, 5, 4, 4, 4, 3, 4, 5, 4, 4, 4, 5, 3, 4, 3, 2, 8, 4,
    4, 5, 6, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 5, 3, 4, 3, 2, 3, 4,
    3, 3, 4, 5, 4, 4, 4, 3, 4, 4, 4, 7, 4, 3, 3, 5, 3, 5, 4, 3,
    4, 5, 3, 4, 5, 5, 4, 2, 2, 5, 4, 4, 3, 5, 2, 5, 2, 3, 4, 2,
    3, 5, 4, 4, 5, 7, 4, 3, 3, 3, 4, 5, 4, 7, 5, 6, 5, 6, 5, 4,
    4, 3, 5, 5, 4, 6, 5, 3, 6, 7, 5, 6, 4, 4, 4, 4, 3, 4, 5, 6,
    4, 2, 3, 5, 2, 2, 3, 2, 4, 5, 3, 6, 3, 4, 4, 4, 3, 3, 5, 4,
    4, 4, 6, 2, 4, 5, 3, 4, 5, 4, 5, 5, 4, 5, 7, 3, 5, 3, 3, 4,
    5, 4, 4, 3, 4, 4, 3, 3, 4, 2, 2, 3, 3, 2, 3, 2, 3, 5, 4, 4,
    5, 4, 4, 4, 4, 3, 4, 5, 5, 3, 4, 3, 3, 5, 5, 4, 4, 5, 4, 5,
    7, 4, 5, 6, 5, 5, 5, 6, 4, 4, 6, 3, 4, 6, 6, 3, 4, 3, 5, 4,
    3, 3, 5, 5, 4, 6, 5, 3, 5, 4, 4, 4, 4, 4, 3, 4, 5, 4, 4, 3,
    4, 2, 4, 2, 5, 5, 4, 3, 5, 4, 6, 4, 5, 6, 5, 2, 2, 4, 5, 4,
    3, 4, 5, 3, 4, 4, 5, 7, 4, 4, 3, 4, 5, 5, 4, 7, 4, 5, 4, 5,
    5, 5, 6, 4, 5, 6, 6, 4, 4, 3, 4, 5, 4, 4, 3, 3, 4, 4, 6, 4,
    4, 3, 6, 5, 6, 4, 6, 4, 5, 5, 4, 4, 5, 6, 5, 4, 4, 5, 2, 3,
    4, 4, 3, 5, 3, 4, 4, 5, 4, 4, 4, 4, 4, 5, 5, 6, 5, 6, 6, 4,
    3, 5, 7, 4, 5, 4, 5, 4, 4, 2, 4, 4, 3, 4, 4, 4, 3, 4, 4, 5,
    6, 3, 5, 5, 5, 6, 4, 4, 5, 3, 3, 3, 2, 3, 4, 4, 6, 6, 4, 5,
    4, 5, 6, 4, 4, 4, 5, 4, 4, 4, 5, 4, 4, 4, 4, 5, 4, 4, 4, 5,
    4, 5, 8, 6, 5, 8, 4, 4, 3, 4, 3, 4, 5, 6, 4, 4, 6, 5, 5, 4,
    3, 3, 4, 6, 4, 4, 6, 5, 5, 4, 6, 5, 5, 4, 7, 7, 3, 6, 6, 4,
    5, 5, 5, 4, 4, 4, 2, 6, 4, 5, 4, 3, 6, 4, 3, 4, 6, 5, 5, 6,
    4, 5, 4, 5, 4, 5, 3, 4, 3, 6, 3, 2, 4, 4, 4, 3, 5, 5, 3, 4,
    3, 6, 4, 5, 5, 4, 3, 4, 6, 6, 7, 6, 5, 6, 5, 4, 5, 4, 5, 4,
    5, 6, 5, 6, 3, 3, 2, 2, 3, 3, 5, 3, 3, 4, 4, 5, 5, 3, 4, 4,
    3, 4, 5, 5, 4, 5, 4, 5, 6, 5, 4, 6, 6, 5, 4, 4, 5, 4, 4, 3,
    5, 5, 2, 2, 3, 5, 5, 4, 5, 4, 4, 7, 8, 5, 6, 4, 3, 4, 5, 5,
    4, 3, 3, 3, 3, 3, 4, 5, 5, 4, 3, 4, 4, 4, 4, 4, 6, 4, 3, 4,
    4, 4, 3, 4, 4, 5, 4, 2, 2, 4, 5, 4, 6, 5, 4, 4, 6, 4, 4, 5,
    2, 2, 3, 4, 4, 4, 3, 4, 6, 5, 5, 5, 9, 5, 4, 3, 3, 4, 3, 4,
    3, 4, 4, 2, 5, 3, 4, 4, 4, 4, 4, 7, 2, 5, 5, 5, 5, 6, 5, 6,
    6, 4, 5, 7, 5, 6, 4, 4, 5, 4, 5, 5, 7, 8, 6, 5, 4, 5, 4, 2,
    3, 5, 7, 7, 3, 4, 2, 4, 3, 3, 4, 6, 5, 4, 3, 3, 6, 3, 3, 4,
    3, 3, 4, 3, 3, 4, 5, 4, 6, 6, 2, 2, 7, 5, 6, 6, 4, 4, 4, 4,
    8, 6, 3, 4, 7, 7, 5, 5, 4, 5, 6, 5, 4, 3, 4, 5, 5, 8, 4, 5,
    5, 5, 6, 4, 4, 4, 5, 5, 4, 4, 5, 5, 3, 6, 4, 5, 5, 5, 2, 4,
    4, 3, 4, 4, 5, 6, 6, 3, 2, 3, 4, 4, 5, 4, 4, 4, 4, 3, 4, 5,
    4, 5, 3, 4, 4, 5, 4, 4, 6, 6, 6, 7, 5, 7, 3, 4, 5, 5, 5, 4,
    6, 4, 4, 5, 4, 4, 4, 5, 5, 3, 5, 6, 4, 3, 4, 4, 4, 4, 5, 6,
    4, 6, 5, 5, 5, 5, 7, 3, 5, 3, 5, 5, 6, 6, 6, 5, 6, 4, 2, 3,
    3, 2, 4, 4, 4, 4, 5, 4, 4, 3, 4, 3, 2, 3, 4, 3, 4, 4, 5, 3,
    4, 5, 4, 4, 5, 5, 4, 5, 5, 5, 4, 4, 5, 4, 5, 5, 4, 4, 5, 4,
    5, 4, 4, 4, 3, 4, 5, 4, 6, 3, 3, 4, 4, 4, 4, 5, 6, 6, 6, 5,
    4, 5, 5, 5, 4, 5, 2, 3, 4, 4, 4, 5, 5, 4, 2, 3, 4, 5, 4, 4,
    5, 3, 2, 4, 3, 4, 5, 5, 6, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 3, 4, 3, 4, 5, 3, 4, 3, 6, 7, 4, 5, 3, 5, 5, 6, 6, 4, 4,
    5, 4, 5, 5, 4, 4, 5, 5, 4, 4, 6, 4, 5, 6, 4, 4, 5, 3, 2, 6,
    4, 5, 5, 4, 3, 5, 3, 2, 4, 4, 4, 5, 4, 4, 4, 4, 4, 2, 3, 5,
    3, 3, 4, 5, 5, 7, 4, 4, 3, 4, 3, 4, 4, 3, 3, 5, 4, 4, 5, 4,
    5, 3, 4, 4, 5, 4, 5, 5, 5, 5, 4, 5, 4, 6, 4, 3, 4, 4, 4, 4,
    5, 3, 4, 5, 4, 5, 3, 5, 5, 4, 5, 5, 4, 4, 4, 3, 2, 3, 5, 3,
    4, 2, 3, 4, 5, 4, 3, 5, 6, 6, 4, 4, 4, 4, 5, 3, 7, 4, 5, 4,
    3, 5, 3, 2, 5, 4, 6, 6, 4, 4, 4, 4, 3, 6, 4, 9, 4, 5, 6, 4,
    4, 4, 5, 6, 8, 3, 5, 2, 3, 3, 4, 4, 4, 4, 3, 4, 4, 3, 4, 3,
    4, 4, 4, 6, 4, 6, 7, 5, 5, 4, 4, 5, 4, 5, 5, 5, 4, 7, 4, 4,
    4, 5, 3, 4, 4, 4, 3, 5, 4, 3, 2, 2, 2, 3, 4, 4, 4, 4, 4, 4,
    3, 6, 5, 4, 4, 5, 3, 3, 4, 5, 6, 5, 6, 6, 4, 5, 5, 4, 4, 3,
    4, 5, 4, 3, 4, 5, 3, 4, 7, 3, 5, 3, 3, 3, 2, 3, 4, 5, 5, 5,
    2, 4, 3, 5, 8, 2, 3, 5, 4, 4, 4, 5, 5, 2, 2, 4, 4, 6, 5, 4,
    5, 6, 4, 3, 4, 4, 4, 3, 6, 6, 5, 4, 4, 3, 4, 3, 5, 6, 4, 4,
    6, 5, 4, 4, 4, 5, 6, 4, 6, 4, 5, 4, 4, 7, 4, 4, 7, 7, 4, 6,
    6, 2, 2, 3, 6, 4, 3, 4, 5, 5, 4, 5, 3, 6, 4, 5, 7, 5, 3, 4,
    3, 6, 3, 5, 3, 3, 4, 4, 5, 5, 5, 8, 4, 7, 5, 5, 3, 4, 6, 5,
    4, 5, 4, 3, 4, 4, 4, 5, 4, 4, 5, 4, 4, 5, 5, 5, 4, 6, 5, 3,
    5, 7, 6, 5, 7, 5, 4, 5, 6, 3, 3, 6, 2, 4, 3, 3, 4, 4, 3, 3,
    4, 3, 6, 3, 2, 2, 5, 3, 2, 3, 3, 4, 4, 5, 5, 2, 3, 3, 6, 8,
    7, 4, 4, 3, 6, 5, 3, 4, 5, 4, 4, 5, 6, 5, 5, 4, 5, 5, 6, 5,
    4, 4, 4, 4, 5, 5, 4, 4, 5, 5, 4, 3, 3, 5, 2, 2, 3, 4, 5, 6,
    3, 2, 3, 8, 5, 4, 4, 4, 3, 4, 3, 3, 5, 4, 4, 5, 6, 4, 4, 4,
    5, 5, 4, 4, 5, 4, 6, 3, 3, 4, 4, 4, 6, 4, 6, 6, 5, 6, 6, 4,
    4, 4, 5, 6, 5, 3, 3, 3, 2, 2, 3, 6, 4, 3, 4, 5, 5, 5, 4, 5,
    5, 4, 5, 3, 5, 6, 6, 7, 6, 4, 6, 6, 3, 6, 5, 3, 4, 5, 5, 3,
    4, 4, 4, 4, 4, 3, 4, 3, 4, 6, 3, 4, 5, 3, 4, 5, 4, 5, 3, 4,
    6, 3, 4, 4, 4, 4, 4, 7, 4, 3, 3, 4, 3, 4, 5, 2, 3, 4, 4, 4,
    5, 5, 5, 3, 2, 3, 2, 2, 3, 6, 4, 3, 4, 4, 6, 4, 4, 4, 6, 5,
    4, 4, 4, 5, 5, 3, 5, 4, 4, 5, 2, 3, 4, 5, 5, 6, 6, 7, 4, 6,
    7, 9, 6, 7, 4, 4, 5, 4, 6, 6, 6, 5, 4, 4, 7, 6, 7, 5, 5, 5,
    6, 6, 8, 8, 8, 5, 4, 4, 4, 2, 3, 6, 3, 3, 2, 4, 3, 7, 3, 2,
    3, 4, 4, 3, 3, 3, 3, 3, 5, 4, 4, 4, 5, 6, 4, 4, 3, 2, 2, 3,
    3, 4, 3, 2, 3, 3, 5, 4, 4, 2, 3, 3, 3, 7, 7, 4, 4, 6, 4, 4,
    4, 3, 3, 3, 4, 4, 5, 3, 5, 5, 3, 4, 4, 4, 3, 3, 3, 3, 4, 5,
    4, 4, 5, 5, 4, 4, 4, 5, 4, 3, 4, 4, 4, 5, 5, 3, 3, 5, 4, 2,
    5, 3, 4, 4, 4, 3, 6, 2, 2, 3, 4, 5, 4, 5, 5, 3, 4, 4, 3, 4,
    5, 5, 5, 5, 5, 3, 3, 3, 5, 4, 5, 4, 3, 4, 3, 4, 4, 5, 3, 5,
    5, 3, 4, 3, 4, 3, 4, 4, 5, 4, 3, 4, 5, 6, 5, 3, 3, 3, 4, 3,
    3, 3, 5, 4, 5, 3, 3, 6, 5, 4, 3, 4, 4, 5, 4, 4, 3, 3, 5, 5,
    4, 6, 4, 6, 4, 4, 4, 4, 3, 7, 4, 4, 4, 4, 5, 5, 5, 4, 5, 3,
    2, 4, 4, 4, 3, 4, 4, 5, 4, 4, 5, 3, 4, 6, 4, 3, 4, 4, 4, 4,
    4, 4, 5, 3, 6, 5, 5, 6, 4, 4, 3, 4, 4, 8, 4, 4, 3, 2, 3, 2,
    3, 3, 2, 3, 3, 5, 4, 3, 4, 6, 5, 4, 4, 4, 5, 4, 6, 3, 5, 6,
    3, 3, 3, 4, 5, 4, 4, 5, 5, 4, 5, 6, 5, 7, 5, 6, 5, 5, 6, 5,
    5, 3, 4, 5, 7, 4, 5, 4, 5, 4, 5, 5, 5, 5, 4, 3, 4, 3, 3, 5,
    5, 3, 4, 4, 4, 4, 4, 4, 4, 3, 4, 5, 4, 4, 4, 4, 4, 3, 7, 3,
    3, 6, 6, 3, 4, 4, 4, 4, 4, 5, 4, 4, 4, 5, 2, 2, 4, 3, 5, 4,
    4, 4, 3, 4, 4, 7, 4, 5, 5, 2, 2, 6, 5, 4, 5, 4, 4, 4, 3, 4,
    3, 3, 2, 6, 3, 6, 5, 4, 3, 4, 4, 3, 2, 4, 4, 3, 2, 5, 3, 5,
    2, 3, 7, 3, 5, 7, 3, 3, 4, 3, 2, 2, 2, 4, 4, 3, 3, 3, 2, 2,
    6, 3, 4, 4, 2, 3, 4, 3, 2, 3, 3, 4, 4, 5, 2, 3, 2, 5, 6, 4,
    2, 5, 2, 3, 2, 3, 2, 3, 3, 3, 2, 2, 3, 4, 3, 4, 5, 4, 4, 2,
    3, 3, 4, 3, 7, 3, 3, 2, 2, 3, 6, 3, 3, 5, 3, 6, 4, 5, 3, 3,
    4, 3, 2, 3, 3, 3, 5, 4, 3, 4, 4, 3, 3, 4, 4, 3, 4, 4, 3, 3,
    3, 3, 5, 4, 4, 3, 3, 3, 4, 5, 4, 5, 4, 2, 2, 3, 3, 2, 2, 3,
    3, 3, 4, 3, 2, 5, 3, 3, 2, 2, 2, 3, 4, 5, 3, 2, 3, 3,
};

static const uint16_t g_en_us_level_offsets[] = {
    0, 4, 7, 10, 13, 16, 19, 23, 27, 31, 34, 39, 42, 45, 48, 51, 54, 57, 63, 68,
    71, 74, 78, 81, 84, 88, 92, 95, 98, 101, 105, 109, 113, 117, 122, 126, 129, 132, 135, 141,
    144, 148, 151, 155, 158, 162, 165, 168, 171, 174, 177, 181, 188, 191, 196, 199, 204, 210, 214, 217,
    221, 224, 227, 230, 235, 238, 241, 246, 249, 252, 258, 261, 264, 267, 274, 278, 282, 286, 289, 295,
    298, 303, 306, 309, 314, 317, 320, 323, 328, 332, 335, 340, 344, 347, 352, 356, 360, 363, 366, 370,
    374, 378, 381, 386, 389, 392, 395, 398, 402, 405, 408, 412, 415, 418, 421, 426, 432, 435, 438, 442,
    447, 450, 454, 457, 461, 465, 469, 472, 476, 479, 482, 485, 490, 494, 498, 502, 505, 510, 514, 518,
    521, 527, 531, 535, 540, 544, 547, 550, 555, 559, 562, 566, 571, 574, 578, 581, 584, 587, 590, 593,
    596, 599, 603, 607, 611, 614, 617, 620, 624, 627, 632, 637, 641, 645, 648, 652, 658, 664, 668, 672,
    675, 679, 682, 685, 689, 692, 696, 700, 703, 707, 712, 717, 722, 726, 732, 738, 744, 750, 756, 759,
    762, 765, 768, 773, 779, 784, 787, 791, 794, 797, 803, 806, 809, 813, 816, 819, 823, 828, 833, 837,
    841, 844, 848, 851, 854, 858, 863, 866, 871, 874, 880, 883, 889, 892, 895, 898, 901, 905, 908, 912,
    915, 919, 923, 927, 930, 931, 933, 935, 939, 942, 946, 951, 954, 956, 960, 961, 964, 967, 969, 971,
    974, 977, 979, 982, 984, 986, 988, 991, 993, 996, 1000, 1003, 1006, 1008, 1011, 1014, 1015, 1017, 1020, 1024,
    1026, 1028, 1030, 1033, 1036, 1039, 1042, 1043, 1045, 1048, 1051, 1056, 1058, 1062, 1065, 1067, 1071, 1074, 1078, 1079,
    1082, 1085, 1088, 1090, 1091, 1094, 1096, 1099, 1101, 1103, 1105, 1108, 1110, 1113, 1115, 1117, 1120, 1123, 1127, 1131,
    1135, 1137, 1140, 1143, 1146, 1148, 1149, 1150, 1153, 1155, 1159, 1162, 1165, 1169, 1172, 1173, 1174, 1176, 1178, 1179,
    1181, 1182, 1183, 1184, 1187, 1190, 1194, 1197, 1199, 1202, 1209, 1212, 1215, 1218, 1221, 1224, 1228, 1230, 1232, 1237,
    1241, 1243, 1246, 1247, 1252, 1254, 1257, 1262, 1264, 1265, 1270, 1273, 1276, 1279, 1281, 1283, 1286, 1288, 1289, 1293,
    1296, 1299, 1301, 1305, 1308, 1310, 1312, 1315, 1317, 1320, 1323, 1325, 1328, 1329, 1333, 1338, 1341, 1347, 1350, 1353,
    1357, 1360, 1363, 1367, 1370, 1373, 1377, 1383, 1388, 1391, 1397, 1402, 1403, 1406, 1409, 1412, 1415, 1417, 1418, 1423,
    1426, 1430, 1433, 1435, 1436, 1438, 1441, 1444, 1447, 1449, 1451, 1456, 1459, 1464, 1469, 1473, 1475, 1480, 1482, 1485,
    1487, 1490, 1493, 1495, 1500, 1504, 1507, 1509, 1512, 1514, 1519, 1524, 1530, 1533, 1539, 1542, 1545, 1548, 1550, 1553,
    1555, 1558, 1561, 1564, 1567, 1570, 1572, 1575, 1578, 1581, 1584, 1587, 1591, 1593, 1595, 1598, 1601, 1604, 1609, 1612,
    1615, 1616, 1619, 1622, 1627, 1629, 1631, 1633, 1637, 1641, 1644, 1646, 1649, 1652, 1655, 1658, 1663, 1666, 1668, 1671,
    1674, 1677, 1680, 1684, 1687, 1690, 1693, 1696, 1701, 1703, 1706, 1709, 1710, 1714, 1716, 1723, 1726, 1730, 1731, 1733,
    1737, 1740, 1743, 1749, 1752, 1754, 1756, 1759, 1761, 1763, 1765, 1768, 1771, 1774, 1777, 1780, 1783, 1785, 1788, 1791,
    1794, 1797, 1799, 1802, 1807, 1812, 1815, 1819, 1824, 1828, 1831, 1834, 1838, 1841, 1843, 1846, 1848, 1852, 1855, 1858,
    1861, 1864, 1868, 1871, 1874, 1877, 1879, 1880, 1883, 1886, 1890, 1893, 1896, 1899, 1903, 1907, 1911, 1915, 1916, 1921,
    1925, 1928, 1932, 1936, 1940, 1944, 1950, 1955, 1959, 1963, 1964, 1967, 1970, 1972, 1974, 1976, 1980, 1984, 1986, 1987,
    1992, 1997, 2000, 2003, 2006, 2009, 2012, 2015, 2016, 2019, 2022, 2025, 2028, 2031, 2035, 2038, 2041, 2042, 2046, 2049,
    2052, 2055, 2058, 2061, 2062, 2064, 2067, 2070, 2074, 2075, 2078, 2081, 2083, 2084, 2087, 2091, 2097, 2101, 2104, 2107,
    2111, 2114, 2118, 2121, 2124, 2127, 2130, 2133, 2137, 2139, 2141, 2144, 2147, 2153, 2159, 2161, 2166, 2167, 2173, 2175,
    2177, 2183, 2188, 2190, 2192, 2196, 2197, 2201, 2204, 2208, 2212, 2216, 2220, 2221, 2222, 2224, 2228, 2229, 2230, 2236,
    2240, 2242, 2247, 2250, 2255, 2261, 2262, 2263, 2269, 2274, 2277, 2282, 2283, 2286, 2288, 2290, 2294, 2297, 2300, 2305,
    2308, 2313, 2316, 2320, 2325, 2330, 2335, 2340, 2341, 2342, 2346, 2347, 2349, 2353, 2355, 2357, 2358, 2362, 2363, 2367,
    2370, 2375, 2380, 2383, 2387, 2389, 2392, 2396, 2401, 2402, 2406, 2410, 2414, 2418, 2422, 2426, 2431, 2434, 2438, 2443,
    2447, 2448, 2454, 2457, 2458, 2462, 2464, 2469, 2473, 2480, 2485, 2490, 2491, 2492, 2493, 2494, 2495, 2496, 2497, 2498,
    2502, 2503, 2504, 2505, 2508, 2509, 2510, 2516, 2520, 2524, 2527, 2531, 2532, 2533, 2536, 2537, 2541, 2545, 2551, 2552,
    2556, 2557, 2560, 2563, 2566, 2571, 2574, 2575, 2578, 2579, 2580, 2581, 2585, 2589, 2590, 2594, 2597, 2598, 2599, 2603,
    2607, 2610, 2611, 2612, 2617, 2618, 2621, 2622, 2623, 2624, 2626, 2627, 2631, 2633, 2635, 2636, 2640, 2645, 2646, 2649,
    2650, 2651, 2652, 2654, 2658, 2659, 2662, 2665, 2668, 2669, 2671, 2672, 2676, 2680, 2681, 2686, 2690, 2692, 2693, 2696,
    2700, 2701, 2704, 2708, 2711, 2715, 2716, 2720, 2724, 2728, 2730, 2734, 2738, 2741, 2747, 2751, 2754, 2755, 2760, 2764,
    2769, 2773, 2778, 2782, 2786, 2789, 2791, 2796, 2797, 2798, 2802, 2803, 2805, 2809, 2813, 2817, 2819, 2824, 2825, 2829,
    2835, 2840, 2844, 2849, 2854, 2858, 2861, 2863, 2867, 2870, 2872, 2874, 2876, 2878, 2883, 2890, 2894, 2896, 2900, 2902,
    2904, 2907, 2908, 2912, 2913, 2916, 2918, 2921, 2922, 2925, 2928, 2933, 2937, 2940, 2941, 2946, 2948, 2951, 2953, 2954,
    2955, 2959, 2961, 2962, 2965, 2970, 2971, 2972, 2974, 2978, 2982, 2987, 2992, 2993, 2994, 2999, 3000, 3001, 3005, 3009,
    3010, 3012, 3014, 3017, 3021, 3024, 3025, 3030, 3033, 3037, 3040, 3046, 3049, 3056, 3060, 3063, 3065, 3066, 3072, 3075,
    3080, 3085, 3088, 3090, 3091, 3092, 3096, 3099, 3102, 3108, 3111, 3112, 3115, 3118, 3124, 3127, 3130, 3133, 3138, 3141,
    3143, 3145, 3146, 3151, 3155, 3159, 3161, 3164, 3168, 3172, 3176, 3179, 3182, 3185, 3188, 3191, 3194, 3198, 3199, 3201,
    3203, 3207, 3210, 3212, 3215, 3216, 3219, 3223, 3227, 3230, 3232, 3233, 3234, 3237, 3239, 3243, 3246, 3249, 3252, 3254,
    3255, 3256, 3257, 3260, 3261, 3265, 3268, 3272, 3275, 3278, 3281, 3286, 3290, 3291, 3295, 3297, 3300, 3302, 3305, 3307,
    3308, 3310, 3311, 3312, 3313, 3317, 3319, 3321, 3322, 3324, 3327, 3328, 3329, 3332, 3333, 3335, 3337, 3340, 3345, 3348,
    3352, 3355, 3360, 3364, 3368, 3370, 3371, 3373, 3376, 3379, 3380, 3382, 3383, 3388, 3389, 3393, 3398, 3399, 3405, 3409,
    3415, 3420, 3428, 3434, 3438, 3439, 3440, 3441, 3444, 3447, 3449, 3451, 3453, 3455, 3456, 3459, 3462, 3465, 3467, 3471,
    3472, 3473, 3476, 3479, 3481, 3485, 3488, 3494, 3495, 3498, 3500, 3502, 3504, 3505, 3508, 3512, 3515, 3517, 3521, 3525,
    3528, 3531, 3534, 3538, 3542, 3546, 3548, 3552, 3556, 3560, 3564, 3568, 3572, 3576, 3580, 3585, 3588, 3590, 3595, 3598,
    3602, 3607, 3609, 3612, 3615, 3619, 3623, 3627, 3629, 3631, 3633, 3635, 3637, 3639, 3641, 3646, 3651, 3653, 3656, 3658,
    3661, 3663, 3666, 3669, 3672, 3675, 3679, 3681, 3683, 3685, 3687, 3689, 3691, 3693, 3695, 3698, 3702, 3704, 3706, 3709,
    3712, 3714, 3716, 3719, 3721, 3724, 3727, 3731, 3735, 3736, 3738, 3741, 3744, 3747, 3750, 3754, 3755, 3757, 3762, 3764,
    3766, 3769, 3772, 3775, 3779, 3782, 3786, 3789, 3792, 3795, 3799, 3803, 3808, 3811, 3813, 3815, 3818, 3819, 3821, 3822,
    3827, 3829, 3833, 3834, 3839, 3841, 3842, 3847, 3850, 3853, 3856, 3861, 3864, 3866, 3868, 3871, 3873, 3874, 3877, 3882,
    3885, 3887, 3890, 3894, 3897, 3899, 3901, 3903, 3905, 3909, 3913, 3916, 3918, 3920, 3922, 3925, 3929, 3932, 3934, 3936,
    3938, 3943, 3946, 3948, 3954, 3956, 3959, 3960, 3962, 3965, 3967, 3970, 3972, 3975, 3978, 3980, 3983, 3986, 3988, 3991,
    3993, 3996, 3998, 4000, 4003, 4008, 4013, 4014, 4017, 4022, 4024, 4026, 4029, 4032, 4035, 4038, 4040, 4043, 4045, 4047,
    4049, 4052, 4055, 4057, 4059, 4061, 4064, 4068, 4071, 4076, 4079, 4083, 4085, 4088, 4090, 4093, 4094, 4098, 4104, 4107,
    4109, 4112, 4116, 4118, 4121, 4123, 4128, 4131, 4134, 4136, 4138, 4141, 4144, 4147, 4150, 4153, 4155, 4158, 4160, 4162,
    4164, 4167, 4170, 4172, 4173, 4174, 4175, 4179, 4181, 4184, 4187, 4191, 4192, 4195, 4198, 4200, 4203, 4205, 4208, 4211,
    4215, 4217, 4221, 4223, 4226, 4229, 4233, 4235, 4238, 4240, 4242, 4244, 4246, 4249, 4251, 4253, 4256, 4261, 4269, 4271,
    4273, 4275, 4278, 4280, 4283, 4285, 4287, 4290, 4295, 4297, 4300, 4302, 4307, 4312, 4315, 4319, 4320, 4323, 4324, 4325,
    4328, 4331, 4334, 4335, 4337, 4341, 4345, 4348, 4351, 4354, 4357, 4358, 4361, 4365, 4368, 4371, 4376, 4379, 4382, 4387,
    4391, 4392, 4394, 4398, 4401, 4403, 4404, 4407, 4411, 4413, 4417, 4420, 4421, 4422, 4425, 4426, 4429, 4431, 4435, 4438,
    4441, 4444, 4448, 4449, 4452, 4453, 4457, 4458, 4461, 4463, 4465, 4468, 4470, 4473, 4475, 4478, 4481, 4483, 4486, 4489,
    4491, 4493, 4495, 4496, 4498, 4502, 4504, 4506, 4508, 4511, 4514, 4517, 4521, 4526, 4530, 4532, 4535, 4537, 4540, 4542,
    4545, 4547, 4553, 4556, 4559, 4562, 4563, 4568, 4574, 4577, 4580, 4583, 4585, 4586, 4588, 4595, 4597, 4600, 4603, 4607,
    4612, 4614, 4620, 4626, 4629, 4631, 4636, 4640, 4642, 4644, 4647, 4648, 4651, 4653, 4655, 4658, 4661, 4664, 4667, 4670,
    4673, 4676, 4677, 4684, 4686, 4688, 4691, 4695, 4700, 4705, 4708, 4713, 4715, 4718, 4720, 4723, 4728, 4730, 4735, 4738,
    4740, 4742, 4745, 4749, 4751, 4755, 4757, 4759, 4761, 4763, 4765, 4767, 4772, 4775, 4777, 4778, 4779, 4780, 4784, 4785,
    4788, 4792, 4795, 4796, 4801, 4806, 4809, 4813, 4814, 4818, 4821, 4824, 4825, 4831, 4833, 4834, 4835, 4840, 4846, 4849,
    4853, 4854, 4855, 4856, 4859, 4862, 4866, 4871, 4875, 4881, 4882, 4886, 4888, 4890, 4892, 4894, 4897, 4899, 4901, 4902,
    4903, 4906, 4908, 4910, 4912, 4913, 4915, 4918, 4919, 4923, 4924, 4925, 4926, 4929, 4932, 4938, 4942, 4947, 4948, 4949,
    4950, 4955, 4958, 4962, 4966, 4970, 4974, 4977, 4982, 4984, 4989, 4993, 4998, 4999, 5003, 5004, 5005, 5006, 5007, 5011,
    5015, 5018, 5021, 5025, 5030, 5034, 5039, 5043, 5045, 5049, 5051, 5056, 5060, 5065, 5070, 5072, 5073, 5075, 5077, 5078,
    5081, 5082, 5085, 5088, 5091, 5096, 5100, 5101, 5102, 5103, 5107, 5108, 5109, 5112, 5113, 5116, 5118, 5122, 5125, 5130,
    5131, 5136, 5141, 5146, 5147, 5148, 5150, 5153, 5154, 5155, 5160, 5164, 5167, 5170, 5171, 5172, 5175, 5179, 5182, 5183,
    5184, 5185, 5189, 5192, 5194, 5195, 5200, 5204, 5205, 5208, 5211, 5214, 5216, 5218, 5223, 5227, 5230, 5233, 5236, 5240,
    5241, 5244, 5248, 5250, 5251, 5253, 5258, 5259, 5263, 5265, 5267, 5271, 5273, 5274, 5275, 5279, 5281, 5284, 5285, 5286,
    5289, 5293, 5298, 5299, 5300, 5302, 5306, 5308, 5310, 5311, 5315, 5319, 5321, 5323, 5326, 5328, 5330, 5335, 5340, 5342,
    5344, 5346, 5348, 5352, 5354, 5357, 5359, 5360, 5361, 5365, 5366, 5369, 5372, 5376, 5379, 5385, 5388, 5393, 5394, 5397,
    5401, 5402, 5406, 5408, 5409, 5410, 5412, 5417, 5418, 5420, 5426, 5427, 5428, 5429, 5433, 5434, 5440, 5441, 5443, 5448,
    5451, 5453, 5457, 5460, 5461, 5462, 5466, 5467, 5468, 5471, 5473, 5475, 5476, 5479, 5482, 5486, 5491, 5495, 5499, 5501,
    5506, 5509, 5514, 5517, 5521, 5525, 5526, 5530, 5535, 5540, 5543, 5547, 5551, 5557, 5561, 5565, 5568, 5571, 5575, 5580,
    5584, 5589, 5593, 5597, 5603, 5608, 5609, 5613, 5615, 5616, 5617, 5620, 5622, 5624, 5629, 5633, 5637, 5641, 5643, 5647,
    5650, 5655, 5659, 5663, 5667, 5669, 5674, 5678, 5683, 5685, 5687, 5689, 5691, 5695, 5698, 5702, 5706, 5711, 5713, 5715,
    5718, 5721, 5726, 5730, 5736, 5738, 5743, 5746, 5753, 5757, 5760, 5764, 5768, 5772, 5776, 5781, 5786, 5789, 5790, 5793,
    5798, 5800, 5804, 5806, 5811, 5813, 5818, 5820, 5822, 5825, 5830, 5834, 5838, 5841, 5846, 5850, 5853, 5854, 5859, 5863,
    5867, 5870, 5875, 5878, 5882, 5885, 5889, 5890, 5896, 5900, 5902, 5905, 5910, 5914, 5918, 5921, 5923, 5925, 5928, 5931,
    5934, 5936, 5939, 5942, 5946, 5950, 5955, 5958, 5960, 5962, 5965, 5968, 5973, 5976, 5978, 5980, 5984, 5988, 5990, 5991,
    5995, 5999, 6002, 6007, 6009, 6012, 6014, 6018, 6021, 6024, 6027, 6030, 6033, 6036, 6038, 6041, 6043, 6045, 6048, 6050,
    6051, 6052, 6053, 6055, 6057, 6062, 6064, 6069, 6070, 6071, 6072, 6074, 6077, 6079, 6082, 6084, 6087, 6089, 6090, 6092,
    6095, 6100, 6104, 6107, 6110, 6113, 6115, 6116, 6118, 6121, 6124, 6128, 6132, 6134, 6137, 6141, 6143, 6146, 6149, 6153,
    6156, 6159, 6161, 6163, 6166, 6168, 6170, 6173, 6176, 6180, 6183, 6188, 6193, 6197, 6199, 6201, 6203, 6205, 6207, 6209,
    6210, 6215, 6218, 6222, 6225, 6226, 6228, 6230, 6231, 6232, 6236, 6239, 6244, 6245, 6247, 6250, 6253, 6256, 6259, 6263,
    6269, 6271, 6274, 6277, 6279, 6283, 6286, 6288, 6291, 6293, 6295, 6296, 6298, 6302, 6304, 6307, 6311, 6313, 6316, 6321,
    6324, 6327, 6330, 6333, 6336, 6339, 6340, 6343, 6347, 6348, 6351, 6354, 6357, 6360, 6362, 6365, 6369, 6376, 6377, 6380,
    6383, 6387, 6389, 6390, 6393, 6395, 6401, 6403, 6406, 6407, 6410, 6411, 6417, 6420, 6421, 6424, 6425, 6427, 6432, 6434,
    6441, 6442, 6443, 6446, 6447, 6450, 6451, 6452, 6453, 6455, 6457, 6460, 6463, 6465, 6467, 6468, 6471, 6472, 6473, 6474,
    6476, 6480, 6484, 6486, 6487, 6490, 6496, 6497, 6500, 6503, 6505, 6506, 6507, 6508, 6513, 6516, 6518, 6521, 6525, 6529,
    6533, 6536, 6540, 6542, 6545, 6547, 6550, 6552, 6553, 6557, 6564, 6567, 6570, 6575, 6578, 6580, 6583, 6586, 6591, 6592,
    6594, 6598, 6600, 6604, 6608, 6610, 6615, 6617, 6620, 6623, 6627, 6630, 6634, 6637, 6640, 6644, 6645, 6651, 6654, 6655,
    6658, 6661, 6666, 6669, 6672, 6673, 6676, 6677, 6680, 6683, 6687, 6690, 6694, 6696, 6699, 6702, 6708, 6709, 6711, 6715,
    6718, 6721, 6724, 6727, 6730, 6736, 6739, 6742, 6745, 6748, 6752, 6753, 6755, 6758, 6759, 6763, 6765, 6769, 6771, 6773,
    6774, 6777, 6779, 6782, 6784, 6785, 6790, 6792, 6793, 6795, 6798, 6800, 6803, 6806, 6808, 6815, 6816, 6817, 6819, 6823,
    6824, 6826, 6829, 6830, 6833, 6835, 6838, 6839, 6841, 6842, 6845, 6848, 6851, 6853, 6856, 6859, 6862, 6864, 6868, 6871,
    6873, 6876, 6877, 6878, 6882, 6883, 6884, 6888, 6893, 6896, 6903, 6904, 6909, 6913, 6914, 6915, 6919, 6922, 6923, 6924,
    6926, 6928, 6933, 6937, 6939, 6941, 6942, 6945, 6948, 6951, 6955, 6957, 6961, 6963, 6966, 6968, 6971, 6973, 6974, 6976,
    6978, 6983, 6985, 6987, 6991, 6993, 6996, 7000, 7003, 7007, 7009, 7012, 7014, 7015, 7016, 7018, 7020, 7022, 7025, 7030,
    7032, 7033, 7037, 7040, 7042, 7044, 7047, 7049, 7051, 7053, 7057, 7059, 7064, 7069, 7071, 7074, 7078, 7083, 7087, 7088,
    7092, 7096, 7100, 7106, 7110, 7114, 7118, 7121, 7122, 7123, 7124, 7128, 7130, 7135, 7138, 7142, 7144, 7151, 7153, 7154,
    7156, 7159, 7162, 7166, 7169, 7171, 7173, 7176, 7181, 7186, 7189, 7195, 7200, 7201, 7207, 7208, 7211, 7215, 7216, 7217,
    7218, 7221, 7222, 7225, 7228, 7233, 7237, 7238, 7240, 7242, 7246, 7249, 7250, 7251, 7252, 7254, 7258, 7262, 7266, 7267,
    7268, 7269, 7271, 7274, 7276, 7281, 7283, 7287, 7289, 7292, 7295, 7300, 7303, 7306, 7309, 7310, 7313, 7314, 7315, 7317,
    7319, 7321, 7325, 7326, 7330, 7332, 7335, 7336, 7337, 7340, 7341, 7344, 7348, 7352, 7355, 7359, 7361, 7363, 7367, 7371,
    7376, 7379, 7381, 7385, 7387, 7389, 7395, 7396, 7399, 7403, 7405, 7406, 7410, 7412, 7416, 7418, 7420, 7422, 7424, 7426,
    7428, 7430, 7434, 7438, 7441, 7444, 7449, 7451, 7454, 7457, 7464, 7467, 7469, 7470, 7472, 7475, 7477, 7482, 7486, 7489,
    7490, 7495, 7500, 7504, 7511, 7514, 7515, 7518, 7519, 7521, 7522, 7526, 7527, 7531, 7534, 7539, 7540, 7544, 7546, 7551,
    7554, 7557, 7561, 7562, 7566, 7567, 7568, 7572, 7575, 7580, 7581, 7582, 7583, 7587, 7589, 7591, 7594, 7596, 7598, 7600,
    7602, 7605, 7607, 7609, 7611, 7612, 7615, 7621, 7623, 7625, 7630, 7636, 7640, 7645, 7647, 7652, 7654, 7658, 7663, 7666,
    7669, 7674, 7677, 7680, 7684, 7688, 7691, 7694, 7698, 7699, 7701, 7702, 7707, 7710, 7714, 7715, 7720, 7724, 7725, 7727,
    7732, 7734, 7735, 7736, 7737, 7742, 7745, 7748, 7752, 7754, 7757, 7758, 7759, 7762, 7765, 7768, 7772, 7773, 7774, 7779,
    7784, 7785, 7788, 7791, 7795, 7799, 7801, 7802, 7806, 7810, 7815, 7816, 7823, 7830, 7833, 7836, 7840, 7844, 7847, 7851,
    7855, 7856, 7861, 7864, 7865, 7867, 7872, 7874, 7877, 7881, 7883, 7884, 7885, 7886, 7887, 7892, 7897, 7900, 7907, 7910,
    7913, 7920, 7924, 7928, 7931, 7936, 7937, 7939, 7943, 7947, 7951, 7952, 7956, 7961, 7967, 7968, 7972, 7975, 7977, 7978,
    7979, 7982, 7986, 7989, 7992, 7994, 7995, 7998, 8001, 8004, 8006, 8007, 8008, 8011, 8015, 8020, 8024, 8028, 8029, 8032,
    8033, 8035, 8040, 8046, 8050, 8051, 8053, 8055, 8059, 8061, 8065, 8069, 8071, 8077, 8083, 8088, 8090, 8094, 8098, 8102,
    8104, 8105, 8107, 8108, 8110, 8112, 8116, 8120, 8122, 8125, 8127, 8130, 8131, 8132, 8138, 8139, 8143, 8147, 8150, 8155,
    8159, 8160, 8165, 8168, 8171, 8175, 8181, 8185, 8190, 8194, 8198, 8201, 8206, 8211, 8217, 8223, 8226, 8229, 8230, 8236,
    8240, 8243, 8246, 8251, 8258, 8260, 8261, 8265, 8268, 8270, 8276, 8280, 8285, 8287, 8293, 8295, 8299, 8302, 8305, 8307,
    8310, 8314, 8319, 8321, 8326, 8329, 8331, 8333, 8336, 8338, 8345, 8347, 8348, 8349, 8352, 8358, 8359, 8365, 8371, 8372,
    8376, 8377, 8380, 8381, 8382, 8385, 8387, 8388, 8391, 8393, 8397, 8401, 8404, 8407, 8408, 8411, 8413, 8419, 8423, 8428,
    8432, 8436, 8437, 8441, 8445, 8447, 8449, 8453, 8457, 8459, 8462, 8467, 8469, 8473, 8474, 8478, 8481, 8486, 8488, 8490,
    8492, 8498, 8500, 8502, 8505, 8508, 8514, 8516, 8518, 8520, 8522, 8524, 8527, 8531, 8533, 8536, 8538, 8540, 8543, 8545,
    8549, 8552, 8553, 8555, 8558, 8561, 8565, 8568, 8569, 8570, 8573, 8577, 8578, 8581, 8585, 8588, 8591, 8592, 8593, 8594,
    8597, 8600, 8602, 8607, 8609, 8611, 8615, 8619, 8620, 8621, 8624, 8625, 8626, 8627, 8628, 8631, 8633, 8636, 8638, 8643,
    8645, 8647, 8652, 8656, 8658, 8660, 8663, 8666, 8668, 8671, 8673, 8675, 8680, 8683, 8686, 8688, 8690, 8692, 8695, 8699,
    8704, 8708, 8710, 8713, 8716, 8719, 8722, 8725, 8728, 8731, 8734, 8737, 8740, 8742, 8746, 8748, 8751, 8753, 8754, 8758,
    8761, 8762, 8766, 8768, 8770, 8771, 8773, 8776, 8778, 8781, 8783, 8785, 8788, 8790, 8793, 8795, 8797, 8799, 8803, 8808,
    8811, 8815, 8819, 8823, 8827, 8830, 8836, 8838, 8841, 8842, 8843, 8848, 8853, 8857, 8858, 8863, 8868, 8869, 8872, 8875,
    8878, 8881, 8884, 8888, 8892, 8896, 8901, 8903, 8904, 8909, 8913, 8917, 8920, 8921, 8925, 8929, 8932, 8933, 8934, 8940,
    8943, 8948, 8951, 8955, 8960, 8962, 8964, 8968, 8971, 8974, 8980, 8983, 8986, 8988, 8991, 8996, 9001, 9003, 9006, 9009,
    9011, 9014, 9016, 9021, 9023, 9027, 9032, 9035, 9037, 9041, 9045, 9047, 9050, 9054, 9059, 9065, 9068, 9072, 9075, 9078,
    9081, 9085, 9087, 9090, 9092, 9095, 9097, 9100, 9102, 9105, 9108, 9111, 9115, 9119, 9120, 9122, 9123, 9127, 9130, 9135,
    9139, 9144, 9148, 9152, 9154, 9158, 9160, 9162, 9165, 9171, 9173, 9176, 9178, 9182, 9184, 9186, 9191, 9193, 9195, 9197,
    9200, 9203, 9208, 9209, 9215, 9218, 9220, 9223, 9228, 9231, 9235, 9240, 9244, 9249, 9252, 9255, 9257, 9259, 9261, 9267,
    9270, 9272, 9274, 9277, 9282, 9286, 9289, 9291, 9293, 9295, 9297, 9299, 9301, 9303, 9306, 9308, 9311, 9312, 9314, 9318,
    9319, 9320, 9323, 9328, 9331, 9335, 9338, 9343, 9347, 9350, 9352, 9357, 9359, 9362, 9367, 9370, 9372, 9373, 9375, 9379,
    9382, 9384, 9386, 9392, 9395, 9399, 9402, 9404, 9409, 9411, 9414, 9417, 9419, 9421, 9424, 9428, 9436, 9438, 9441, 9443,
    9445, 9448, 9450, 9452, 9454, 9459, 9462, 9467, 9471, 9474, 9477, 9480, 9483, 9486, 9488, 9491, 9493, 9496, 9500, 9503,
    9506, 9509, 9511, 9518, 9521, 9524, 9526, 9529, 9531, 9534, 9537, 9539, 9542, 9544, 9545, 9549, 9551, 9554, 9560, 9562,
    9565, 9568, 9570, 9573, 9574, 9575, 9578, 9580, 9583, 9586, 9589, 9591, 9594, 9596, 9599, 9602, 9607, 9610, 9612, 9618,
    9620, 9623, 9629, 9636, 9641, 9647, 9651, 9657, 9660, 9665, 9668, 9672, 9676, 9679, 9683, 9685, 9690, 9692, 9696, 9699,
    9700, 9701, 9702, 9704, 9706, 9709, 9712, 9714, 9716, 9718, 9720, 9723, 9727, 9729, 9731, 9733, 9736, 9739, 9742, 9746,
    9748, 9751, 9755, 9758, 9763, 9766, 9768, 9771, 9773, 9776, 9778, 9781, 9783, 9786, 9790, 9792, 9795, 9798, 9799, 9802,
    9806, 9811, 9814, 9822, 9827, 9830, 9833, 9836, 9838, 9840, 9843, 9846, 9849, 9851, 9852, 9854, 9858, 9861, 9864, 9867,
    9871, 9874, 9876, 9879, 9882, 9885, 9888, 9890, 9893, 9896, 9898, 9904, 9907, 9910, 9912, 9918, 9923, 9924, 9930, 9934,
    9939, 9942, 9945, 9947, 9949, 9951, 9955, 9958, 9963, 9966, 9971, 9974, 9977, 9982, 9986, 9989, 9992, 9997, 10002, 10005,
    10009, 10011, 10017, 10020, 10023, 10026, 10029, 10032, 10036, 10038, 10043, 10046, 10047, 10050, 10053, 10057, 10059, 10060, 10062, 10064,
    10069, 10071, 10075, 10079, 10083, 10087, 10090, 10093, 10096, 10101, 10105, 10113, 10118, 10124, 10129, 10133, 10134, 10138, 10141, 10145,
    10148, 10151, 10152, 10155, 10158, 10162, 10163, 10165, 10168, 10169, 10172, 10177, 10180, 10182, 10183, 10184, 10190, 10194, 10196, 10200,
    10205, 10209, 10212, 10217, 10220, 10222, 10226, 10229, 10231, 10236, 10238, 10240, 10245, 10249, 10255, 10257, 10261, 10265, 10268, 10272,
    10275, 10278, 10281, 10282, 10283, 10284, 10289, 10293, 10296, 10299, 10302, 10303, 10308, 10316, 10319, 10320, 10321, 10324, 10325, 10326,
    10327, 10331, 10332, 10335, 10336, 10337, 10340, 10345, 10350, 10353, 10356, 10358, 10360, 10363, 10364, 10365, 10368, 10371, 10374, 10377,
    10379, 10384, 10386, 10389, 10394, 10396, 10399, 10402, 10405, 10408, 10409, 10414, 10418, 10423, 10427, 10428, 10432, 10436, 10441, 10445,
    10450, 10452, 10454, 10457, 10458, 10461, 10466, 10469, 10474, 10479, 10480, 10481, 10486, 10493, 10498, 10501, 10504, 10506, 10509, 10510,
    10514, 10516, 10519, 10520, 10522, 10526, 10528, 10530, 10532, 10534, 10536, 10538, 10542, 10545, 10550, 10551, 10555, 10559, 10564, 10568,
    10574, 10578, 10582, 10584, 10589, 10590, 10594, 10598, 10602, 10608, 10612, 10617, 10619, 10623, 10629, 10634, 10639, 10643, 10648, 10652,
    10655, 10657, 10662, 10670, 10678, 10681, 10683, 10685, 10689, 10691, 10693, 10697, 10704, 10710, 10714, 10716, 10720, 10726, 10730, 10734,
    10738, 10741, 10744, 10748, 10749, 10752, 10753, 10757, 10760, 10764, 10768, 10770, 10773, 10780, 10786, 10793, 10800, 10804, 10805, 10806,
    10807, 10815, 10820, 10821, 10824, 10826, 10831, 10836, 10839, 10841, 10844, 10850, 10854, 10858, 10860, 10863, 10865, 10869, 10874, 10876,
    10879, 10884, 10888, 10892, 10894, 10897, 10900, 10904, 10905, 10909, 10914, 10918, 10919, 10921, 10928, 10932, 10935, 10939, 10942, 10944,
    10946, 10948, 10952, 10956, 10958, 10960, 10963, 10968, 10971, 10973, 10975, 10980, 10982, 10986, 10990, 10993, 10998, 11000, 11003, 11007,
    11011, 11016, 11019, 11020, 11023, 11026, 11029, 11030, 11033, 11035, 11040, 11044, 11048, 11051, 11056, 11058, 11061, 11064, 11068, 11071,
    11074, 11077, 11080, 11083, 11087, 11090, 11093, 11096, 11100, 11104, 11107, 11110, 11113, 11116, 11120, 11122, 11127, 11130, 11132, 11135,
    11140, 11144, 11147, 11150, 11153, 11158, 11161, 11165, 11169, 11172, 11176, 11179, 11183, 11187, 11190, 11193, 11196, 11200, 11203, 11205,
    11209, 11211, 11214, 11217, 11219, 11221, 11225, 11228, 11230, 11232, 11234, 11238, 11240, 11243, 11244, 11245, 11248, 11252, 11255, 11257,
    11259, 11263, 11267, 11269, 11270, 11271, 11274, 11276, 11280, 11283, 11286, 11289, 11292, 11296, 11297, 11301, 11302, 11306, 11307, 11311,
    11313, 11314, 11318, 11322, 11326, 11329, 11330, 11335, 11338, 11342, 11344, 11346, 11348, 11352, 11354, 11358, 11363, 11365, 11369, 11373,
    11377, 11380, 11384, 11388, 11392, 11396, 11401, 11403, 11404, 11406, 11409, 11412, 11415, 11417, 11418, 11420, 11424, 11426, 11430, 11432,
    11434, 11437, 11441, 11443, 11447, 11449, 11451, 11454, 11457, 11459, 11461, 11463, 11465, 11467, 11469, 11471, 11473, 11475, 11477, 11482,
    11484, 11486, 11490, 11492, 11494, 11498, 11504, 11506, 11509, 11512, 11516, 11524, 11527, 11530, 11534, 11537, 11538, 11541, 11545, 11549,
    11553, 11557, 11561, 11565, 11569, 11570, 11571, 11574, 11578, 11582, 11585, 11589, 11593, 11596, 11599, 11602, 11605, 11609, 11613, 11615,
    11620, 11622, 11624, 11627, 11629, 11633, 11636, 11638, 11641, 11645, 11649, 11651, 11655, 11659, 11663, 11668, 11673, 11678, 11681, 11683,
    11687, 11690, 11692, 11694, 11698, 11701, 11708, 11711, 11716, 11718, 11720, 11722, 11726, 11731, 11733, 11735, 11741, 11743, 11745, 11750,
    11754, 11756, 11758, 11761, 11765, 11767, 11769, 11774, 11779, 11781, 11783, 11785, 11787, 11789, 11796, 11799, 11802, 11806, 11809, 11812,
    11815, 11819, 11822, 11827, 11831, 11833, 11838, 11841, 11843, 11848, 11850, 11853, 11855, 11857, 11859, 11863, 11865, 11867, 11870, 11871,
    11874, 11877, 11880, 11882, 11883, 11887, 11889, 11890, 11896, 11902, 11908, 11912, 11915, 11919, 11920, 11924, 11926, 11932, 11935, 11938,
    11942, 11946, 11949, 11950, 11952, 11958, 11962, 11967, 11972, 11977, 11979, 11980, 11982, 11986, 11988, 11990, 12000, 12002, 12007, 12011,
    12016, 12018, 12023, 12024, 12030, 12036, 12038, 12044, 12046, 12047, 12050, 12055, 12059, 12063, 12064, 12066, 12070, 12072, 12075, 12079,
    12080, 12083, 12084, 12085, 12090, 12091, 12097, 12103, 12106, 12110, 12111, 12115, 12117, 12121, 12123, 12124, 12125, 12130, 12134, 12136,
    12138, 12142, 12143, 12146, 12149, 12153, 12156, 12157, 12161, 12165, 12166, 12168, 12170, 12172, 12173, 12176, 12177, 12180, 12183, 12184,
    12189, 12193, 12198, 12201, 12206, 12211, 12217, 12218, 12221, 12223, 12224, 12228, 12229, 12234, 12239, 12240, 12243, 12244, 12248, 12249,
    12251, 12253, 12254, 12256, 12257, 12258, 12263, 12266, 12270, 12273, 12274, 12275, 12276, 12277, 12278, 12281, 12282, 12284, 12287, 12290,
    12294, 12297, 12299, 12301, 12307, 12313, 12315, 12317, 12323, 12328, 12333, 12335, 12336, 12342, 12345, 12346, 12349, 12354, 12359, 12362,
    12367, 12370, 12373, 12374, 12375, 12377, 12382, 12386, 12388, 12389, 12391, 12393, 12397, 12401, 12402, 12405, 12406, 12408, 12410, 12411,
    12415, 12419, 12424, 12426, 12427, 12429, 12430, 12436, 12441, 12446, 12451, 12454, 12455, 12457, 12464, 12466, 12471, 12478, 12485, 12486,
    12492, 12497, 12499, 12500, 12502, 12509, 12514, 12517, 12519, 12521, 12523, 12525, 12528, 12530, 12534, 12536, 12540, 12547, 12550, 12552,
    12555, 12557, 12564, 12567, 12572, 12575, 12576, 12578, 12580, 12585, 12587, 12588, 12594, 12598, 12603, 12604, 12608, 12611, 12613, 12619,
    12621, 12625, 12630, 12632, 12635, 12637, 12639, 12641, 12642, 12644, 12646, 12649, 12650, 12652, 12653, 12658, 12659, 12661, 12666, 12667,
    12670, 12672, 12679, 12680, 12682, 12688, 12690, 12691, 12694, 12701, 12704, 12706, 12711, 12712, 12715, 12719, 12723, 12726, 12731, 12733,
    12736, 12740, 12743, 12750, 12753, 12754, 12757, 12763, 12764, 12766, 12767, 12768, 12769, 12773, 12776, 12782, 12783, 12784, 12785, 12788,
    12793, 12794, 12795, 12798, 12802, 12805, 12809, 12812, 12815, 12819, 12823, 12824, 12830, 12834, 12837, 12840, 12843, 12847, 12853, 12857,
    12860, 12863, 12867, 12868, 12869, 12872, 12876, 12880, 12883, 12884, 12887, 12892, 12896, 12897, 12901, 12903, 12904, 12906, 12910, 12914,
    12919, 12921, 12923, 12924, 12930, 12935, 12936, 12941, 12942, 12944, 12947, 12948, 12952, 12955, 12958, 12959, 12964, 12970, 12975, 12976,
    12981, 12985, 12986, 12987, 12988, 12989, 12990, 12994, 12995, 12998, 13001, 13005, 13009, 13013, 13014, 13018, 13022, 13026, 13031, 13032,
    13036, 13037, 13038, 13040, 13045, 13050, 13051, 13052, 13053, 13055, 13057, 13058, 13063, 13068, 13071, 13072, 13075, 13079, 13083, 13084,
    13087, 13090, 13091, 13092, 13093, 13096, 13097, 13101, 13108, 13113, 13114, 13119, 13123, 13124, 13129, 13133, 13134, 13137, 13140, 13143,
    13146, 13147, 13149, 13153, 13154, 13155, 13159, 13162, 13163, 13164, 13169, 13170, 13171, 13175, 13177, 13179, 13180, 13181, 13186, 13187,
    13190, 13195, 13196, 13199, 13200, 13204, 13207, 13211, 13212, 13215, 13218, 13219, 13223, 13224, 13227, 13230, 13231, 13233, 13238, 13239,
    13240, 13241, 13243, 13247, 13249, 13251, 13255, 13258, 13259, 13262, 13265, 13266, 13267, 13270, 13273, 13278, 13281, 13285, 13288, 13292,
    13295, 13296, 13297, 13300, 13303, 13307, 13311, 13315, 13316, 13317, 13320, 13322, 13323, 13327, 13331, 13337, 13342, 13347, 13355, 13360,
    13364, 13368, 13377, 13384, 13391, 13395, 13399, 13404, 13405, 13408, 13412, 13413, 13414, 13415, 13419, 13424, 13428, 13434, 13438, 13443,
    13444, 13448, 13453, 13459, 13465, 13472, 13476, 13480, 13484, 13489, 13492, 13494, 13499, 13503, 13505, 13508, 13510, 13512, 13520, 13524,
    13525, 13528, 13531, 13534, 13538, 13539, 13543, 13546, 13547, 13550, 13552, 13553, 13554, 13558, 13563, 13567, 13570, 13571, 13572, 13575,
    13577, 13579, 13584, 13585, 13586, 13587, 13588, 13592, 13597, 13600, 13601, 13604, 13605, 13609, 13612, 13616, 13619, 13623, 13627, 13631,
    13635, 13639, 13641, 13645, 13648, 13650, 13652, 13654, 13657, 13660, 13662, 13664, 13668, 13671, 13676, 13678, 13680, 13682, 13685, 13688,
    13691, 13696, 13698, 13701, 13704, 13707, 13709, 13711, 13715, 13718, 13720, 13724, 13726, 13731, 13735, 13739, 13740, 13742, 13746, 13749,
    13752, 13756, 13759, 13761, 13765, 13770, 13774, 13778, 13780, 13781, 13783, 13787, 13789, 13794, 13795, 13798, 13801, 13803, 13806, 13809,
    13811, 13814, 13817, 13820, 13825, 13828, 13831, 13834, 13835, 13839, 13842, 13848, 13849, 13851, 13854, 13857, 13860, 13863, 13866, 13868,
    13871, 13876, 13879, 13884, 13886, 13889, 13891, 13894, 13896, 13899, 13903, 13907, 13910, 13914, 13917, 13920, 13924, 13927, 13930, 13932,
    13934, 13936, 13938, 13943, 13945, 13948, 13950, 13953, 13960, 13964, 13969, 13971, 13972, 13974, 13977, 13979, 13982, 13986, 13989, 13993,
    13996, 13999, 14003, 14005, 14009, 14012, 14014, 14016, 14019, 14022, 14026, 14029, 14034, 14037, 14040, 14044, 14047, 14050, 14055, 14058,
    14060, 14061, 14063, 14065, 14068, 14072, 14075, 14079, 14082, 14084, 14086, 14089, 14092, 14095, 14098, 14101, 14103, 14108, 14112, 14114,
    14115, 14116, 14117, 14122, 14125, 14129, 14131, 14134, 14140, 14142, 14144, 14147, 14150, 14154, 14158, 14160, 14162, 14166, 14168, 14171,
    14174, 14178, 14181, 14182, 14183, 14186, 14190, 14194, 14198, 14201, 14204, 14207, 14211, 14215, 14218, 14221, 14224, 14228, 14229, 14231,
    14237, 14238, 14239, 14243, 14245, 14249, 14252, 14254, 14258, 14260, 14262, 14263, 14267, 14269, 14270, 14272, 14276, 14278, 14282, 14288,
    14289, 14293, 14296, 14297, 14301, 14305, 14308, 14312, 14315, 14318, 14319, 14320, 14321, 14323, 14324, 14325, 14327, 14330, 14334, 14335,
    14336, 14339, 14341, 14344, 14346, 14350, 14351, 14355, 14359, 14362, 14365, 14368, 14372, 14375, 14378, 14379, 14383, 14384, 14385, 14392,
    14393, 14397, 14403, 14410, 14411, 14414, 14416, 14417, 14418, 14422, 14426, 14431, 14434, 14437, 14438, 14441, 14443, 14445, 14446, 14449,
    14453, 14458, 14460, 14464, 14468, 14472, 14480, 14483, 14489, 14492, 14494, 14496, 14500, 14506, 14510, 14515, 14520, 14524, 14528, 14530,
    14535, 14537, 14541, 14544, 14549, 14553, 14558, 14562, 14566, 14570, 14571, 14576, 14580, 14582, 14585, 14588, 14590, 14593, 14597, 14601,
    14604, 14606, 14610, 14615, 14619, 14625, 14630, 14632, 14635, 14638, 14641, 14642, 14645, 14647, 14651, 14653, 14657, 14659, 14663, 14666,
    14668, 14671, 14673, 14677, 14680, 14682, 14686, 14691, 14695, 14697, 14700, 14703, 14706, 14708, 14711, 14713, 14715, 14717, 14722, 14729,
    14733, 14735, 14741, 14744, 14746, 14748, 14751, 14754, 14756, 14760, 14762, 14764, 14766, 14768, 14771, 14773, 14777, 14782, 14787, 14792,
    14794, 14796, 14798, 14800, 14804, 14811, 14814, 14816, 14818, 14820, 14822, 14827, 14829, 14831, 14837, 14841, 14845, 14850, 14853, 14856,
    14859, 14862, 14863, 14866, 14869, 14872, 14876, 14879, 14881, 14883, 14885, 14890, 14893, 14895, 14897, 14900, 14902, 14906, 14909, 14911,
    14914, 14917, 14920, 14923, 14926, 14927, 14929, 14933, 14936, 14939, 14941, 14946, 14948, 14951, 14953, 14956, 14959, 14963, 14964, 14965,
    14968, 14971, 14973, 14976, 14980, 14982, 14984, 14986, 14988, 14989, 14990, 14991, 14994, 14997, 15000, 15004, 15007, 15010,
};

static const uint8_t g_en_us_level_lengths[] = {
    4, 3, 3, 3, 3, 3, 4, 4, 4, 3, 5, 3, 3, 3, 3, 3, 3, 6, 5, 3,
    3, 4, 3, 3, 4, 4, 3, 3, 3, 4, 4, 4, 4, 5, 4, 3, 3, 3, 6, 3,
    4, 3, 4, 3, 4, 3, 3, 3, 3, 3, 4, 7, 3, 5, 3, 5, 6, 4, 3, 4,
    3, 3, 3, 5, 3, 3, 5, 3, 3, 6, 3, 3, 3, 7, 4, 4, 4, 3, 6, 3,
    5, 3, 3, 5, 3, 3, 3, 5, 4, 3, 5, 4, 3, 5, 4, 4, 3, 3, 4, 4,
    4, 3, 5, 3, 3, 3, 3, 4, 3, 3, 4, 3, 3, 3, 5, 6, 3, 3, 4, 5,
    3, 4, 3, 4, 4, 4, 3, 4, 3, 3, 3, 5, 4, 4, 4, 3, 5, 4, 4, 3,
    6, 4, 4, 5, 4, 3, 3, 5, 4, 3, 4, 5, 3, 4, 3, 3, 3, 3, 3, 3,
    3, 4, 4, 4, 3, 3, 3, 4, 3, 5, 5, 4, 4, 3, 4, 6, 6, 4, 4, 3,
    4, 3, 3, 4, 3, 4, 4, 3, 4, 5, 5, 5, 4, 6, 6, 6, 6, 6, 3, 3,
    3, 3, 5, 6, 5, 3, 4, 3, 3, 6, 3, 3, 4, 3, 3, 4, 5, 5, 4, 4,
    3, 4, 3, 3, 4, 5, 3, 5, 3, 6, 3, 6, 3, 3, 3, 3, 4, 3, 4, 3,
    4, 4, 4, 3, 1, 2, 2, 4, 3, 4, 5, 3, 2, 4, 1, 3, 3, 2, 2, 3,
    3, 2, 3, 2, 2, 2, 3, 2, 3, 4, 3, 3, 2, 3, 3, 1, 2, 3, 4, 2,
    2, 2, 3, 3, 3, 3, 1, 2, 3, 3, 5, 2, 4, 3, 2, 4, 3, 4, 1, 3,
    3, 3, 2, 1, 3, 2, 3, 2, 2, 2, 3, 2, 3, 2, 2, 3, 3, 4, 4, 4,
    2, 3, 3, 3, 2, 1, 1, 3, 2, 4, 3, 3, 4, 3, 1, 1, 2, 2, 1, 2,
    1, 1, 1, 3, 3, 4, 3, 2, 3, 7, 3, 3, 3, 3, 3, 4, 2, 2, 5, 4,
    2, 3, 1, 5, 2, 3, 5, 2, 1, 5, 3, 3, 3, 2, 2, 3, 2, 1, 4, 3,
    3, 2, 4, 3, 2, 2, 3, 2, 3, 3, 2, 3, 1, 4, 5, 3, 6, 3, 3, 4,
    3, 3, 4, 3, 3, 4, 6, 5, 3, 6, 5, 1, 3, 3, 3, 3, 2, 1, 5, 3,
    4, 3, 2, 1, 2, 3, 3, 3, 2, 2, 5, 3, 5, 5, 4, 2, 5, 2, 3, 2,
    3, 3, 2, 5, 4, 3, 2, 3, 2, 5, 5, 6, 3, 6, 3, 3, 3, 2, 3, 2,
    3, 3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 4, 2, 2, 3, 3, 3, 5, 3, 3,
    1, 3, 3, 5, 2, 2, 2, 4, 4, 3, 2, 3, 3, 3, 3, 5, 3, 2, 3, 3,
    3, 3, 4, 3, 3, 3, 3, 5, 2, 3, 3, 1, 4, 2, 7, 3, 4, 1, 2, 4,
    3, 3, 6, 3, 2, 2, 3, 2, 2, 2, 3, 3, 3, 3, 3, 3, 2, 3, 3, 3,
    3, 2, 3, 5, 5, 3, 4, 5, 4, 3, 3, 4, 3, 2, 3, 2, 4, 3, 3, 3,
    3, 4, 3, 3, 3, 2, 1, 3, 3, 4, 3, 3, 3, 4, 4, 4, 4, 1, 5, 4,
    3, 4, 4, 4, 4, 6, 5, 4, 4, 1, 3, 3, 2, 2, 2, 4, 4, 2, 1, 5,
    5, 3, 3, 3, 3, 3, 3, 1, 3, 3, 3, 3, 3, 4, 3, 3, 1, 4, 3, 3,
    3, 3, 3, 1, 2, 3, 3, 4, 1, 3, 3, 2, 1, 3, 4, 6, 4, 3, 3, 4,
    3, 4, 3, 3, 3, 3, 3, 4, 2, 2, 3, 3, 6, 6, 2, 5, 1, 6, 2, 2,
    6, 5, 2, 2, 4, 1, 4, 3, 4, 4, 4, 4, 1, 1, 2, 4, 1, 1, 6, 4,
    2, 5, 3, 5, 6, 1, 1, 6, 5, 3, 5, 1, 3, 2, 2, 4, 3, 3, 5, 3,
    5, 3, 4, 5, 5, 5, 5, 1, 1, 4, 1, 2, 4, 2, 2, 1, 4, 1, 4, 3,
    5, 5, 3, 4, 2, 3, 4, 5, 1, 4, 4, 4, 4, 4, 4, 5, 3, 4, 5, 4,
    1, 6, 3, 1, 4, 2, 5, 4, 7, 5, 5, 1, 1, 1, 1, 1, 1, 1, 1, 4,
    1, 1, 1, 3, 1, 1, 6, 4, 4, 3, 4, 1, 1, 3, 1, 4, 4, 6, 1, 4,
    1, 3, 3, 3, 5, 3, 1, 3, 1, 1, 1, 4, 4, 1, 4, 3, 1, 1, 4, 4,
    3, 1, 1, 5, 1, 3, 1, 1, 1, 2, 1, 4, 2, 2, 1, 4, 5, 1, 3, 1,
    1, 1, 2, 4, 1, 3, 3, 3, 1, 2, 1, 4, 4, 1, 5, 4, 2, 1, 3, 4,
    1, 3, 4, 3, 4, 1, 4, 4, 4, 2, 4, 4, 3, 6, 4, 3, 1, 5, 4, 5,
    4, 5, 4, 4, 3, 2, 5, 1, 1, 4, 1, 2, 4, 4, 4, 2, 5, 1, 4, 6,
    5, 4, 5, 5, 4, 3, 2, 4, 3, 2, 2, 2, 2, 5, 7, 4, 2, 4, 2, 2,
    3, 1, 4, 1, 3, 2, 3, 1, 3, 3, 5, 4, 3, 1, 5, 2, 3, 2, 1, 1,
    4, 2, 1, 3, 5, 1, 1, 2, 4, 4, 5, 5, 1, 1, 5, 1, 1, 4, 4, 1,
    2, 2, 3, 4, 3, 1, 5, 3, 4, 3, 6, 3, 7, 4, 3, 2, 1, 6, 3, 5,
    5, 3, 2, 1, 1, 4, 3, 3, 6, 3, 1, 3, 3, 6, 3, 3, 3, 5, 3, 2,
    2, 1, 5, 4, 4, 2, 3, 4, 4, 4, 3, 3, 3, 3, 3, 3, 4, 1, 2, 2,
    4, 3, 2, 3, 1, 3, 4, 4, 3, 2, 1, 1, 3, 2, 4, 3, 3, 3, 2, 1,
    1, 1, 3, 1, 4, 3, 4, 3, 3, 3, 5, 4, 1, 4, 2, 3, 2, 3, 2, 1,
    2, 1, 1, 1, 4, 2, 2, 1, 2, 3, 1, 1, 3, 1, 2, 2, 3, 5, 3, 4,
    3, 5, 4, 4, 2, 1, 2, 3, 3, 1, 2, 1, 5, 1, 4, 5, 1, 6, 4, 6,
    5, 8, 6, 4, 1, 1, 1, 3, 3, 2, 2, 2, 2, 1, 3, 3, 3, 2, 4, 1,
    1, 3, 3, 2, 4, 3, 6, 1, 3, 2, 2, 2, 1, 3, 4, 3, 2, 4, 4, 3,
    3, 3, 4, 4, 4, 2, 4, 4, 4, 4, 4, 4, 4, 4, 5, 3, 2, 5, 3, 4,
    5, 2, 3, 3, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 5, 5, 2, 3, 2, 3,
    2, 3, 3, 3, 3, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 2, 2, 3, 3,
    2, 2, 3, 2, 3, 3, 4, 4, 1, 2, 3, 3, 3, 3, 4, 1, 2, 5, 2, 2,
    3, 3, 3, 4, 3, 4, 3, 3, 3, 4, 4, 5, 3, 2, 2, 3, 1, 2, 1, 5,
    2, 4, 1, 5, 2, 1, 5, 3, 3, 3, 5, 3, 2, 2, 3, 2, 1, 3, 5, 3,
    2, 3, 4, 3, 2, 2, 2, 2, 4, 4, 3, 2, 2, 2, 3, 4, 3, 2, 2, 2,
    5, 3, 2, 6, 2, 3, 1, 2, 3, 2, 3, 2, 3, 3, 2, 3, 3, 2, 3, 2,
    3, 2, 2, 3, 5, 5, 1, 3, 5, 2, 2, 3, 3, 3, 3, 2, 3, 2, 2, 2,
    3, 3, 2, 2, 2, 3, 4, 3, 5, 3, 4, 2, 3, 2, 3, 1, 4, 6, 3, 2,
    3, 4, 2, 3, 2, 5, 3, 3, 2, 2, 3, 3, 3, 3, 3, 2, 3, 2, 2, 2,
    3, 3, 2, 1, 1, 1, 4, 2, 3, 3, 4, 1, 3, 3, 2, 3, 2, 3, 3, 4,
    2, 4, 2, 3, 3, 4, 2, 3, 2, 2, 2, 2, 3, 2, 2, 3, 5, 8, 2, 2,
    2, 3, 2, 3, 2, 2, 3, 5, 2, 3, 2, 5, 5, 3, 4, 1, 3, 1, 1, 3,
    3, 3, 1, 2, 4, 4, 3, 3, 3, 3, 1, 3, 4, 3, 3, 5, 3, 3, 5, 4,
    1, 2, 4, 3, 2, 1, 3, 4, 2, 4, 3, 1, 1, 3, 1, 3, 2, 4, 3, 3,
    3, 4, 1, 3, 1, 4, 1, 3, 2, 2, 3, 2, 3, 2, 3, 3, 2, 3, 3, 2,
    2, 2, 1, 2, 4, 2, 2, 2, 3, 3, 3, 4, 5, 4, 2, 3, 2, 3, 2, 3,
    2, 6, 3, 3, 3, 1, 5, 6, 3, 3, 3, 2, 1, 2, 7, 2, 3, 3, 4, 5,
    2, 6, 6, 3, 2, 5, 4, 2, 2, 3, 1, 3, 2, 2, 3, 3, 3, 3, 3, 3,
    3, 1, 7, 2, 2, 3, 4, 5, 5, 3, 5, 2, 3, 2, 3, 5, 2, 5, 3, 2,
    2, 3, 4, 2, 4, 2, 2, 2, 2, 2, 2, 5, 3, 2, 1, 1, 1, 4, 1, 3,
    4, 3, 1, 5, 5, 3, 4, 1, 4, 3, 3, 1, 6, 2, 1, 1, 5, 6, 3, 4,
    1, 1, 1, 3, 3, 4, 5, 4, 6, 1, 4, 2, 2, 2, 2, 3, 2, 2, 1, 1,
    3, 2, 2, 2, 1, 2, 3, 1, 4, 1, 1, 1, 3, 3, 6, 4, 5, 1, 1, 1,
    5, 3, 4, 4, 4, 4, 3, 5, 2, 5, 4, 5, 1, 4, 1, 1, 1, 1, 4, 4,
    3, 3, 4, 5, 4, 5, 4, 2, 4, 2, 5, 4, 5, 5, 2, 1, 2, 2, 1, 3,
    1, 3, 3, 3, 5, 4, 1, 1, 1, 4, 1, 1, 3, 1, 3, 2, 4, 3, 5, 1,
    5, 5, 5, 1, 1, 2, 3, 1, 1, 5, 4, 3, 3, 1, 1, 3, 4, 3, 1, 1,
    1, 4, 3, 2, 1, 5, 4, 1, 3, 3, 3, 2, 2, 5, 4, 3, 3, 3, 4, 1,
    3, 4, 2, 1, 2, 5, 1, 4, 2, 2, 4, 2, 1, 1, 4, 2, 3, 1, 1, 3,
    4, 5, 1, 1, 2, 4, 2, 2, 1, 4, 4, 2, 2, 3, 2, 2, 5, 5, 2, 2,
    2, 2, 4, 2, 3, 2, 1, 1, 4, 1, 3, 3, 4, 3, 6, 3, 5, 1, 3, 4,
    1, 4, 2, 1, 1, 2, 5, 1, 2, 6, 1, 1, 1, 4, 1, 6, 1, 2, 5, 3,
    2, 4, 3, 1, 1, 4, 1, 1, 3, 2, 2, 1, 3, 3, 4, 5, 4, 4, 2, 5,
    3, 5, 3, 4, 4, 1, 4, 5, 5, 3, 4, 4, 6, 4, 4, 3, 3, 4, 5, 4,
    5, 4, 4, 6, 5, 1, 4, 2, 1, 1, 3, 2, 2, 5, 4, 4, 4, 2, 4, 3,
    5, 4, 4, 4, 2, 5, 4, 5, 2, 2, 2, 2, 4, 3, 4, 4, 5, 2, 2, 3,
    3, 5, 4, 6, 2, 5, 3, 7, 4, 3, 4, 4, 4, 4, 5, 5, 3, 1, 3, 5,
    2, 4, 2, 5, 2, 5, 2, 2, 3, 5, 4, 4, 3, 5, 4, 3, 1, 5, 4, 4,
    3, 5, 3, 4, 3, 4, 1, 6, 4, 2, 3, 5, 4, 4, 3, 2, 2, 3, 3, 3,
    2, 3, 3, 4, 4, 5, 3, 2, 2, 3, 3, 5, 3, 2, 2, 4, 4, 2, 1, 4,
    4, 3, 5, 2, 3, 2, 4, 3, 3, 3, 3, 3, 3, 2, 3, 2, 2, 3, 2, 1,
    1, 1, 2, 2, 5, 2, 5, 1, 1, 1, 2, 3, 2, 3, 2, 3, 2, 1, 2, 3,
    5, 4, 3, 3, 3, 2, 1, 2, 3, 3, 4, 4, 2, 3, 4, 2, 3, 3, 4, 3,
    3, 2, 2, 3, 2, 2, 3, 3, 4, 3, 5, 5, 4, 2, 2, 2, 2, 2, 2, 1,
    5, 3, 4, 3, 1, 2, 2, 1, 1, 4, 3, 5, 1, 2, 3, 3, 3, 3, 4, 6,
    2, 3, 3, 2, 4, 3, 2, 3, 2, 2, 1, 2, 4, 2, 3, 4, 2, 3, 5, 3,
    3, 3, 3, 3, 3, 1, 3, 4, 1, 3, 3, 3, 3, 2, 3, 4, 7, 1, 3, 3,
    4, 2, 1, 3, 2, 6, 2, 3, 1, 3, 1, 6, 3, 1, 3, 1, 2, 5, 2, 7,
    1, 1, 3, 1, 3, 1, 1, 1, 2, 2, 3, 3, 2, 2, 1, 3, 1, 1, 1, 2,
    4, 4, 2, 1, 3, 6, 1, 3, 3, 2, 1, 1, 1, 5, 3, 2, 3, 4, 4, 4,
    3, 4, 2, 3, 2, 3, 2, 1, 4, 7, 3, 3, 5, 3, 2, 3, 3, 5, 1, 2,
    4, 2, 4, 4, 2, 5, 2, 3, 3, 4, 3, 4, 3, 3, 4, 1, 6, 3, 1, 3,
    3, 5, 3, 3, 1, 3, 1, 3, 3, 4, 3, 4, 2, 3, 3, 6, 1, 2, 4, 3,
    3, 3, 3, 3, 6, 3, 3, 3, 3, 4, 1, 2, 3, 1, 4, 2, 4, 2, 2, 1,
    3, 2, 3, 2, 1, 5, 2, 1, 2, 3, 2, 3, 3, 2, 7, 1, 1, 2, 4, 1,
    2, 3, 1, 3, 2, 3, 1, 2, 1, 3, 3, 3, 2, 3, 3, 3, 2, 4, 3, 2,
    3, 1, 1, 4, 1, 1, 4, 5, 3, 7, 1, 5, 4, 1, 1, 4, 3, 1, 1, 2,
    2, 5, 4, 2, 2, 1, 3, 3, 3, 4, 2, 4, 2, 3, 2, 3, 2, 1, 2, 2,
    5, 2, 2, 4, 2, 3, 4, 3, 4, 2, 3, 2, 1, 1, 2, 2, 2, 3, 5, 2,
    1, 4, 3, 2, 2, 3, 2, 2, 2, 4, 2, 5, 5, 2, 3, 4, 5, 4, 1, 4,
    4, 4, 6, 4, 4, 4, 3, 1, 1, 1, 4, 2, 5, 3, 4, 2, 7, 2, 1, 2,
    3, 3, 4, 3, 2, 2, 3, 5, 5, 3, 6, 5, 1, 6, 1, 3, 4, 1, 1, 1,
    3, 1, 3, 3, 5, 4, 1, 2, 2, 4, 3, 1, 1, 1, 2, 4, 4, 4, 1, 1,
    1, 2, 3, 2, 5, 2, 4, 2, 3, 3, 5, 3, 3, 3, 1, 3, 1, 1, 2, 2,
    2, 4, 1, 4, 2, 3, 1, 1, 3, 1, 3, 4, 4, 3, 4, 2, 2, 4, 4, 5,
    3, 2, 4, 2, 2, 6, 1, 3, 4, 2, 1, 4, 2, 4, 2, 2, 2, 2, 2, 2,
    2, 4, 4, 3, 3, 5, 2, 3, 3, 7, 3, 2, 1, 2, 3, 2, 5, 4, 3, 1,
    5, 5, 4, 7, 3, 1, 3, 1, 2, 1, 4, 1, 4, 3, 5, 1, 4, 2, 5, 3,
    3, 4, 1, 4, 1, 1, 4, 3, 5, 1, 1, 1, 4, 2, 2, 3, 2, 2, 2, 2,
    3, 2, 2, 2, 1, 3, 6, 2, 2, 5, 6, 4, 5, 2, 5, 2, 4, 5, 3, 3,
    5, 3, 3, 4, 4, 3, 3, 4, 1, 2, 1, 5, 3, 4, 1, 5, 4, 1, 2, 5,
    2, 1, 1, 1, 5, 3, 3, 4, 2, 3, 1, 1, 3, 3, 3, 4, 1, 1, 5, 5,
    1, 3, 3, 4, 4, 2, 1, 4, 4, 5, 1, 7, 7, 3, 3, 4, 4, 3, 4, 4,
    1, 5, 3, 1, 2, 5, 2, 3, 4, 2, 1, 1, 1, 1, 5, 5, 3, 7, 3, 3,
    7, 4, 4, 3, 5, 1, 2, 4, 4, 4, 1, 4, 5, 6, 1, 4, 3, 2, 1, 1,
    3, 4, 3, 3, 2, 1, 3, 3, 3, 2, 1, 1, 3, 4, 5, 4, 4, 1, 3, 1,
    2, 5, 6, 4, 1, 2, 2, 4, 2, 4, 4, 2, 6, 6, 5, 2, 4, 4, 4, 2,
    1, 2, 1, 2, 2, 4, 4, 2, 3, 2, 3, 1, 1, 6, 1, 4, 4, 3, 5, 4,
    1, 5, 3, 3, 4, 6, 4, 5, 4, 4, 3, 5, 5, 6, 6, 3, 3, 1, 6, 4,
    3, 3, 5, 7, 2, 1, 4, 3, 2, 6, 4, 5, 2, 6, 2, 4, 3, 3, 2, 3,
    4, 5, 2, 5, 3, 2, 2, 3, 2, 7, 2, 1, 1, 3, 6, 1, 6, 6, 1, 4,
    1, 3, 1, 1, 3, 2, 1, 3, 2, 4, 4, 3, 3, 1, 3, 2, 6, 4, 5, 4,
    4, 1, 4, 4, 2, 2, 4, 4, 2, 3, 5, 2, 4, 1, 4, 3, 5, 2, 2, 2,
    6, 2, 2, 3, 3, 6, 2, 2, 2, 2, 2, 3, 4, 2, 3, 2, 2, 3, 2, 4,
    3, 1, 2, 3, 3, 4, 3, 1, 1, 3, 4, 1, 3, 4, 3, 3, 1, 1, 1, 3,
    3, 2, 5, 2, 2, 4, 4, 1, 1, 3, 1, 1, 1, 1, 3, 2, 3, 2, 5, 2,
    2, 5, 4, 2, 2, 3, 3, 2, 3, 2, 2, 5, 3, 3, 2, 2, 2, 3, 4, 5,
    4, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 4, 2, 3, 2, 1, 4, 3,
    1, 4, 2, 2, 1, 2, 3, 2, 3, 2, 2, 3, 2, 3, 2, 2, 2, 4, 5, 3,
    4, 4, 4, 4, 3, 6, 2, 3, 1, 1, 5, 5, 4, 1, 5, 5, 1, 3, 3, 3,
    3, 3, 4, 4, 4, 5, 2, 1, 5, 4, 4, 3, 1, 4, 4, 3, 1, 1, 6, 3,
    5, 3, 4, 5, 2, 2, 4, 3, 3, 6, 3, 3, 2, 3, 5, 5, 2, 3, 3, 2,
    3, 2, 5, 2, 4, 5, 3, 2, 4, 4, 2, 3, 4, 5, 6, 3, 4, 3, 3, 3,
    4, 2, 3, 2, 3, 2, 3, 2, 3, 3, 3, 4, 4, 1, 2, 1, 4, 3, 5, 4,
    5, 4, 4, 2, 4, 2, 2, 3, 6, 2, 3, 2, 4, 2, 2, 5, 2, 2, 2, 3,
    3, 5, 1, 6, 3, 2, 3, 5, 3, 4, 5, 4, 5, 3, 3, 2, 2, 2, 6, 3,
    2, 2, 3, 5, 4, 3, 2, 2, 2, 2, 2, 2, 2, 3, 2, 3, 1, 2, 4, 1,
    1, 3, 5, 3, 4, 3, 5, 4, 3, 2, 5, 2, 3, 5, 3, 2, 1, 2, 4, 3,
    2, 2, 6, 3, 4, 3, 2, 5, 2, 3, 3, 2, 2, 3, 4, 8, 2, 3, 2, 2,
    3, 2, 2, 2, 5, 3, 5, 4, 3, 3, 3, 3, 3, 2, 3, 2, 3, 4, 3, 3,
    3, 2, 7, 3, 3, 2, 3, 2, 3, 3, 2, 3, 2, 1, 4, 2, 3, 6, 2, 3,
    3, 2, 3, 1, 1, 3, 2, 3, 3, 3, 2, 3, 2, 3, 3, 5, 3, 2, 6, 2,
    3, 6, 7, 5, 6, 4, 6, 3, 5, 3, 4, 4, 3, 4, 2, 5, 2, 4, 3, 1,
    1, 1, 2, 2, 3, 3, 2, 2, 2, 2, 3, 4, 2, 2, 2, 3, 3, 3, 4, 2,
    3, 4, 3, 5, 3, 2, 3, 2, 3, 2, 3, 2, 3, 4, 2, 3, 3, 1, 3, 4,
    5, 3, 8, 5, 3, 3, 3, 2, 2, 3, 3, 3, 2, 1, 2, 4, 3, 3, 3, 4,
    3, 2, 3, 3, 3, 3, 2, 3, 3, 2, 6, 3, 3, 2, 6, 5, 1, 6, 4, 5,
    3, 3, 2, 2, 2, 4, 3, 5, 3, 5, 3, 3, 5, 4, 3, 3, 5, 5, 3, 4,
    2, 6, 3, 3, 3, 3, 3, 4, 2, 5, 3, 1, 3, 3, 4, 2, 1, 2, 2, 5,
    2, 4, 4, 4, 4, 3, 3, 3, 5, 4, 8, 5, 6, 5, 4, 1, 4, 3, 4, 3,
    3, 1, 3, 3, 4, 1, 2, 3, 1, 3, 5, 3, 2, 1, 1, 6, 4, 2, 4, 5,
    4, 3, 5, 3, 2, 4, 3, 2, 5, 2, 2, 5, 4, 6, 2, 4, 4, 3, 4, 3,
    3, 3, 1, 1, 1, 5, 4, 3, 3, 3, 1, 5, 8, 3, 1, 1, 3, 1, 1, 1,
    4, 1, 3, 1, 1, 3, 5, 5, 3, 3, 2, 2, 3, 1, 1, 3, 3, 3, 3, 2,
    5, 2, 3, 5, 2, 3, 3, 3, 3, 1, 5, 4, 5, 4, 1, 4, 4, 5, 4, 5,
    2, 2, 3, 1, 3, 5, 3, 5, 5, 1, 1, 5, 7, 5, 3, 3, 2, 3, 1, 4,
    2, 3, 1, 2, 4, 2, 2, 2, 2, 2, 2, 4, 3, 5, 1, 4, 4, 5, 4, 6,
    4, 4, 2, 5, 1, 4, 4, 4, 6, 4, 5, 2, 4, 6, 5, 5, 4, 5, 4, 3,
    2, 5, 8, 8, 3, 2, 2, 4, 2, 2, 4, 7, 6, 4, 2, 4, 6, 4, 4, 4,
    3, 3, 4, 1, 3, 1, 4, 3, 4, 4, 2, 3, 7, 6, 7, 7, 4, 1, 1, 1,
    8, 5, 1, 3, 2, 5, 5, 3, 2, 3, 6, 4, 4, 2, 3, 2, 4, 5, 2, 3,
    5, 4, 4, 2, 3, 3, 4, 1, 4, 5, 4, 1, 2, 7, 4, 3, 4, 3, 2, 2,
    2, 4, 4, 2, 2, 3, 5, 3, 2, 2, 5, 2, 4, 4, 3, 5, 2, 3, 4, 4,
    5, 3, 1, 3, 3, 3, 1, 3, 2, 5, 4, 4, 3, 5, 2, 3, 3, 4, 3, 3,
    3, 3, 3, 4, 3, 3, 3, 4, 4, 3, 3, 3, 3, 4, 2, 5, 3, 2, 3, 5,
    4, 3, 3, 3, 5, 3, 4, 4, 3, 4, 3, 4, 4, 3, 3, 3, 4, 3, 2, 4,
    2, 3, 3, 2, 2, 4, 3, 2, 2, 2, 4, 2, 3, 1, 1, 3, 4, 3, 2, 2,
    4, 4, 2, 1, 1, 3, 2, 4, 3, 3, 3, 3, 4, 1, 4, 1, 4, 1, 4, 2,
    1, 4, 4, 4, 3, 1, 5, 3, 4, 2, 2, 2, 4, 2, 4, 5, 2, 4, 4, 4,
    3, 4, 4, 4, 4, 5, 2, 1, 2, 3, 3, 3, 2, 1, 2, 4, 2, 4, 2, 2,
    3, 4, 2, 4, 2, 2, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 5, 2,
    2, 4, 2, 2, 4, 6, 2, 3, 3, 4, 8, 3, 3, 4, 3, 1, 3, 4, 4, 4,
    4, 4, 4, 4, 1, 1, 3, 4, 4, 3, 4, 4, 3, 3, 3, 3, 4, 4, 2, 5,
    2, 2, 3, 2, 4, 3, 2, 3, 4, 4, 2, 4, 4, 4, 5, 5, 5, 3, 2, 4,
    3, 2, 2, 4, 3, 7, 3, 5, 2, 2, 2, 4, 5, 2, 2, 6, 2, 2, 5, 4,
    2, 2, 3, 4, 2, 2, 5, 5, 2, 2, 2, 2, 2, 7, 3, 3, 4, 3, 3, 3,
    4, 3, 5, 4, 2, 5, 3, 2, 5, 2, 3, 2, 2, 2, 4, 2, 2, 3, 1, 3,
    3, 3, 2, 1, 4, 2, 1, 6, 6, 6, 4, 3, 4, 1, 4, 2, 6, 3, 3, 4,
    4, 3, 1, 2, 6, 4, 5, 5, 5, 2, 1, 2, 4, 2, 2, 10, 2, 5, 4, 5,
    2, 5, 1, 6, 6, 2, 6, 2, 1, 3, 5, 4, 4, 1, 2, 4, 2, 3, 4, 1,
    3, 1, 1, 5, 1, 6, 6, 3, 4, 1, 4, 2, 4, 2, 1, 1, 5, 4, 2, 2,
    4, 1, 3, 3, 4, 3, 1, 4, 4, 1, 2, 2, 2, 1, 3, 1, 3, 3, 1, 5,
    4, 5, 3, 5, 5, 6, 1, 3, 2, 1, 4, 1, 5, 5, 1, 3, 1, 4, 1, 2,
    2, 1, 2, 1, 1, 5, 3, 4, 3, 1, 1, 1, 1, 1, 3, 1, 2, 3, 3, 4,
    3, 2, 2, 6, 6, 2, 2, 6, 5, 5, 2, 1, 6, 3, 1, 3, 5, 5, 3, 5,
    3, 3, 1, 1, 2, 5, 4, 2, 1, 2, 2, 4, 4, 1, 3, 1, 2, 2, 1, 4,
    4, 5, 2, 1, 2, 1, 6, 5, 5, 5, 3, 1, 2, 7, 2, 5, 7, 7, 1, 6,
    5, 2, 1, 2, 7, 5, 3, 2, 2, 2, 2, 3, 2, 4, 2, 4, 7, 3, 2, 3,
    2, 7, 3, 5, 3, 1, 2, 2, 5, 2, 1, 6, 4, 5, 1, 4, 3, 2, 6, 2,
    4, 5, 2, 3, 2, 2, 2, 1, 2, 2, 3, 1, 2, 1, 5, 1, 2, 5, 1, 3,
    2, 7, 1, 2, 6, 2, 1, 3, 7, 3, 2, 5, 1, 3, 4, 4, 3, 5, 2, 3,
    4, 3, 7, 3, 1, 3, 6, 1, 2, 1, 1, 1, 4, 3, 6, 1, 1, 1, 3, 5,
    1, 1, 3, 4, 3, 4, 3, 3, 4, 4, 1, 6, 4, 3, 3, 3, 4, 6, 4, 3,
    3, 4, 1, 1, 3, 4, 4, 3, 1, 3, 5, 4, 1, 4, 2, 1, 2, 4, 4, 5,
    2, 2, 1, 6, 5, 1, 5, 1, 2, 3, 1, 4, 3, 3, 1, 5, 6, 5, 1, 5,
    4, 1, 1, 1, 1, 1, 4, 1, 3, 3, 4, 4, 4, 1, 4, 4, 4, 5, 1, 4,
    1, 1, 2, 5, 5, 1, 1, 1, 2, 2, 1, 5, 5, 3, 1, 3, 4, 4, 1, 3,
    3, 1, 1, 1, 3, 1, 4, 7, 5, 1, 5, 4, 1, 5, 4, 1, 3, 3, 3, 3,
    1, 2, 4, 1, 1, 4, 3, 1, 1, 5, 1, 1, 4, 2, 2, 1, 1, 5, 1, 3,
    5, 1, 3, 1, 4, 3, 4, 1, 3, 3, 1, 4, 1, 3, 3, 1, 2, 5, 1, 1,
    1, 2, 4, 2, 2, 4, 3, 1, 3, 3, 1, 1, 3, 3, 5, 3, 4, 3, 4, 3,
    1, 1, 3, 3, 4, 4, 4, 1, 1, 3, 2, 1, 4, 4, 6, 5, 5, 8, 5, 4,
    4, 9, 7, 7, 4, 4, 5, 1, 3, 4, 1, 1, 1, 4, 5, 4, 6, 4, 5, 1,
    4, 5, 6, 6, 7, 4, 4, 4, 5, 3, 2, 5, 4, 2, 3, 2, 2, 8, 4, 1,
    3, 3, 3, 4, 1, 4, 3, 1, 3, 2, 1, 1, 4, 5, 4, 3, 1, 1, 3, 2,
    2, 5, 1, 1, 1, 1, 4, 5, 3, 1, 3, 1, 4, 3, 4, 3, 4, 4, 4, 4,
    4, 2, 4, 3, 2, 2, 2, 3, 3, 2, 2, 4, 3, 5, 2, 2, 2, 3, 3, 3,
    5, 2, 3, 3, 3, 2, 2, 4, 3, 2, 4, 2, 5, 4, 4, 1, 2, 4, 3, 3,
    4, 3, 2, 4, 5, 4, 4, 2, 1, 2, 4, 2, 5, 1, 3, 3, 2, 3, 3, 2,
    3, 3, 3, 5, 3, 3, 3, 1, 4, 3, 6, 1, 2, 3, 3, 3, 3, 3, 2, 3,
    5, 3, 5, 2, 3, 2, 3, 2, 3, 4, 4, 3, 4, 3, 3, 4, 3, 3, 2, 2,
    2, 2, 5, 2, 3, 2, 3, 7, 4, 5, 2, 1, 2, 3, 2, 3, 4, 3, 4, 3,
    3, 4, 2, 4, 3, 2, 2, 3, 3, 4, 3, 5, 3, 3, 4, 3, 3, 5, 3, 2,
    1, 2, 2, 3, 4, 3, 4, 3, 2, 2, 3, 3, 3, 3, 3, 2, 5, 4, 2, 1,
    1, 1, 5, 3, 4, 2, 3, 6, 2, 2, 3, 3, 4, 4, 2, 2, 4, 2, 3, 3,
    4, 3, 1, 1, 3, 4, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 4, 1, 2, 6,
    1, 1, 4, 2, 4, 3, 2, 4, 2, 2, 1, 4, 2, 1, 2, 4, 2, 4, 6, 1,
    4, 3, 1, 4, 4, 3, 4, 3, 3, 1, 1, 1, 2, 1, 1, 2, 3, 4, 1, 1,
    3, 2, 3, 2, 4, 1, 4, 4, 3, 3, 3, 4, 3, 3, 1, 4, 1, 1, 7, 1,
    4, 6, 7, 1, 3, 2, 1, 1, 4, 4, 5, 3, 3, 1, 3, 2, 2, 1, 3, 4,
    5, 2, 4, 4, 4, 8, 3, 6, 3, 2, 2, 4, 6, 4, 5, 5, 4, 4, 2, 5,
    2, 4, 3, 5, 4, 5, 4, 4, 4, 1, 5, 4, 2, 3, 3, 2, 3, 4, 4, 3,
    2, 4, 5, 4, 6, 5, 2, 3, 3, 3, 1, 3, 2, 4, 2, 4, 2, 4, 3, 2,
    3, 2, 4, 3, 2, 4, 5, 4, 2, 3, 3, 3, 2, 3, 2, 2, 2, 5, 7, 4,
    2, 6, 3, 2, 2, 3, 3, 2, 4, 2, 2, 2, 2, 3, 2, 4, 5, 5, 5, 2,
    2, 2, 2, 4, 7, 3, 2, 2, 2, 2, 5, 2, 2, 6, 4, 4, 5, 3, 3, 3,
    3, 1, 3, 3, 3, 4, 3, 2, 2, 2, 5, 3, 2, 2, 3, 2, 4, 3, 2, 3,
    3, 3, 3, 3, 1, 2, 4, 3, 3, 2, 5, 2, 3, 2, 3, 3, 4, 1, 1, 3,
    3, 2, 3, 4, 2, 2, 2, 2, 1, 1, 1, 3, 3, 3, 4, 3, 3, 2,
};

static const uint8_t g_en_us_level_data[] = {
    0, 0, 0, 4, 0, 0, 4, 0, 0, 1, 0, 0, 3, 0, 0, 5, 0, 0, 5, 0,
    0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 0, 5, 0,
    0, 5, 0, 0, 4, 0, 0, 4, 0, 0, 3, 0, 0, 1, 0, 0, 1, 0, 0, 0,
    0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 1, 0, 0, 4, 0, 0, 0, 4, 0, 0,
    4, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 0, 3, 0, 0,
    5, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0,
    0, 5, 0, 0, 0, 5, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0,
    5, 0, 0, 2, 0, 0, 0, 5, 0, 0, 3, 0, 0, 0, 5, 0, 0, 4, 0, 0,
    0, 5, 0, 0, 4, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 4, 0, 0, 4, 0,
    0, 0, 0, 5, 0, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 4, 0, 0, 0,
    4, 0, 0, 2, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 3, 0, 0,
    3, 0, 0, 5, 0, 5, 0, 0, 4, 0, 0, 3, 0, 0, 0, 1, 6, 1, 0, 0,
    3, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 5, 0, 0,
    0, 3, 0, 0, 0, 5, 0, 0, 2, 0, 0, 4, 1, 0, 1, 0, 0, 2, 0, 0,
    0, 3, 4, 0, 0, 5, 0, 0, 4, 0, 0, 5, 0, 1, 0, 0, 5, 0, 0, 4,
    0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 2, 0, 0, 3, 0, 1,
    0, 0, 3, 6, 0, 0, 3, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 3,
    0, 0, 3, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0,
    4, 0, 0, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 1, 0, 0, 3, 0, 0,
    0, 2, 0, 0, 2, 0, 0, 3, 0, 0, 5, 2, 0, 0, 5, 0, 0, 4, 0, 0,
    3, 0, 0, 0, 2, 3, 0, 0, 6, 1, 0, 1, 0, 0, 4, 0, 0, 4, 0, 0,
    0, 5, 0, 0, 0, 0, 5, 0, 0, 2, 0, 0, 0, 5, 0, 0, 6, 0, 0, 0,
    4, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 5, 0, 0, 2, 0,
    0, 3, 0, 0, 4, 0, 0, 0, 5, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0,
    0, 5, 0, 0, 2, 0, 0, 4, 0, 1, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0,
    5, 0, 0, 0, 5, 2, 1, 0, 0, 0, 1, 0, 0, 1, 4, 0, 0, 0, 0, 5,
    0, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 6, 3, 0,
    0, 4, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0, 0, 2, 0, 0, 0, 5, 0, 0,
    5, 0, 0, 5, 0, 0, 3, 0, 0, 1, 0, 0, 3, 0, 0, 3, 0, 0, 4, 0,
    0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 4,
    0, 0, 0, 5, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,
    1, 0, 0, 0, 3, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 5, 0, 0,
    0, 5, 0, 1, 0, 0, 0, 2, 0, 0, 1, 4, 0, 0, 5, 0, 0, 0, 2, 0,
    0, 5, 0, 0, 4, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 5,
    0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0,
    0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 4, 0, 0,
    0, 0, 0, 4, 0, 0, 0, 6, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 2, 0,
    0, 5, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0, 0, 6, 0, 1, 0,
    0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 5, 0, 0, 2, 0, 0, 4, 0, 0, 5,
    0, 0, 1, 0, 0, 4, 0, 0, 3, 0, 0, 0, 5, 0, 0, 2, 0, 0, 2, 0,
    0, 0, 4, 0, 0, 0, 5, 5, 0, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0,
    4, 0, 0, 4, 0, 0, 0, 5, 0, 0, 6, 0, 0, 2, 0, 0, 0, 5, 0, 0,
    0, 0, 5, 0, 0, 1, 0, 0, 3, 0, 5, 0, 0, 3, 0, 0, 0, 0, 0, 5,
    0, 0, 1, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0,
    3, 0, 0, 0, 3, 0, 0, 5, 0, 0, 0, 4, 0, 0, 5, 0, 0, 2, 3, 0,
    0, 2, 1, 0, 0, 0, 5, 0, 0, 4, 4, 0, 5, 0, 5, 0, 0, 0, 2, 0,
    0, 5, 0, 0, 0, 5, 0, 0, 5, 0, 5, 0, 0, 5, 0, 5, 0, 0, 5, 5,
    4, 0, 0, 5, 0, 0, 3, 0, 2, 0, 4, 0, 0, 5, 0, 0, 5, 0, 5, 0,
    0, 1, 0, 5, 4, 2, 0, 3, 0, 0, 1, 0, 3, 0, 0, 5, 0, 0, 0, 5,
    0, 0, 3, 0, 0, 4, 0, 2, 0, 0, 4, 0, 0, 5, 2, 0, 3, 0, 0, 3,
    0, 0, 0, 4, 0, 3, 0, 3, 0, 5, 0, 0, 4, 0, 0, 3, 0, 0, 5, 0,
    0, 4, 4, 0, 3, 0, 0, 5, 0, 0, 4, 0, 0, 0, 0, 4, 0, 2, 0, 0,
    0, 4, 0, 0, 6, 0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 4, 4, 0,
    0, 1, 4, 0, 4, 0, 0, 1, 0, 2, 3, 0, 0, 3, 0, 5, 0, 0, 5, 0,
    4, 0, 3, 0, 3, 0, 0, 4, 0, 3, 0, 0, 2, 0, 5, 0, 3, 0, 0, 5,
    0, 4, 4, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 1, 0, 0, 1,
    0, 0, 5, 0, 0, 3, 0, 4, 4, 2, 0, 0, 3, 0, 4, 0, 5, 0, 5, 0,
    0, 1, 0, 0, 4, 0, 0, 0, 4, 0, 0, 5, 4, 4, 0, 5, 0, 4, 4, 5,
    5, 5, 3, 4, 0, 0, 5, 0, 0, 3, 0, 0, 0, 5, 0, 0, 5, 0, 4, 0,
    4, 5, 0, 0, 1, 0, 3, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0,
    5, 0, 0, 1, 0, 0, 0, 4, 0, 2, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0,
    5, 0, 2, 0, 0, 3, 3, 0, 0, 0, 0, 6, 0, 3, 0, 0, 3, 0, 0, 0,
    0, 4, 0, 3, 4, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 1, 0, 0, 4, 0,
    5, 0, 3, 0, 0, 5, 0, 3, 2, 0, 0, 0, 5, 0, 0, 1, 0, 4, 1, 0,
    3, 0, 0, 3, 3, 0, 0, 4, 0, 5, 0, 5, 0, 0, 3, 0, 3, 0, 0, 3,
    0, 0, 3, 0, 3, 0, 0, 4, 5, 0, 0, 0, 4, 0, 0, 0, 5, 2, 0, 0,
    5, 0, 0, 0, 0, 0, 5, 0, 0, 2, 0, 0, 4, 0, 0, 0, 5, 0, 0, 4,
    0, 0, 2, 0, 0, 0, 3, 0, 0, 4, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0,
    0, 0, 4, 0, 0, 0, 0, 1, 0, 0, 4, 0, 0, 3, 0, 1, 2, 0, 0, 0,
    0, 1, 4, 0, 0, 2, 0, 0, 4, 0, 0, 3, 0, 0, 3, 0, 5, 4, 0, 0,
    0, 0, 4, 0, 0, 5, 0, 4, 0, 5, 0, 0, 5, 0, 3, 4, 0, 4, 0, 0,
    5, 0, 0, 3, 0, 0, 3, 0, 3, 0, 2, 0, 0, 0, 0, 5, 0, 0, 5, 0,
    0, 0, 0, 5, 0, 0, 0, 0, 3, 0, 0, 0, 5, 0, 3, 0, 0, 0, 0, 5,
    2, 2, 0, 0, 3, 0, 5, 0, 0, 5, 0, 0, 3, 0, 5, 0, 0, 0, 0, 4,
    0, 0, 0, 3, 0, 0, 4, 0, 5, 0, 0, 5, 0, 5, 0, 0, 5, 0, 4, 0,
    0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 3, 0, 0, 5, 0,
    0, 5, 0, 0, 4, 0, 0, 5, 0, 3, 0, 0, 3, 0, 5, 0, 0, 4, 0, 0,
    4, 0, 0, 1, 0, 0, 5, 0, 0, 3, 0, 3, 0, 0, 4, 0, 0, 5, 0, 0,
    3, 0, 0, 2, 0, 0, 2, 0, 0, 5, 5, 0, 5, 0, 3, 0, 0, 2, 0, 0,
    3, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 2, 4, 0, 0, 4, 0,
    0, 3, 0, 0, 0, 0, 4, 0, 5, 0, 3, 0, 3, 5, 5, 0, 4, 0, 0, 0,
    3, 0, 0, 4, 0, 4, 0, 0, 5, 0, 0, 4, 0, 0, 3, 0, 0, 1, 0, 0,
    0, 0, 5, 0, 0, 5, 0, 2, 0, 0, 3, 0, 0, 5, 0, 0, 3, 0, 0, 5,
    0, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 0, 0,
    5, 0, 5, 0, 0, 3, 0, 0, 5, 4, 0, 0, 0, 5, 0, 5, 0, 0, 0, 3,
    0, 1, 1, 0, 0, 4, 0, 0, 0, 5, 4, 0, 5, 0, 0, 5, 5, 0, 0, 1,
    0, 0, 3, 0, 0, 0, 0, 0, 5, 0, 0, 3, 0, 4, 0, 2, 0, 0, 5, 0,
    4, 0, 4, 0, 1, 0, 0, 5, 0, 0, 4, 0, 0, 4, 0, 0, 5, 0, 0, 6,
    0, 0, 4, 0, 2, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 3, 0, 2, 0,
    0, 4, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 4, 2, 0,
    0, 5, 0, 5, 0, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 0, 5, 0, 0,
    1, 0, 2, 0, 0, 3, 0, 5, 0, 0, 0, 4, 0, 0, 3, 0, 0, 5, 0, 0,
    5, 0, 0, 1, 0, 0, 0, 4, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 1, 3,
    0, 0, 3, 0, 0, 4, 0, 0, 0, 4, 0, 0, 4, 0, 0, 4, 0, 0, 5, 0,
    0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 5, 0, 0, 0, 0,
    2, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 4,
    0, 0, 0, 3, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 0, 6, 1, 0,
    0, 0, 4, 1, 0, 0, 1, 0, 0, 4, 2, 1, 0, 2, 0, 3, 0, 0, 0, 4,
    0, 0, 0, 4, 4, 1, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 3, 4, 0, 2,
    0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 5, 1, 0, 0, 1, 0,
    0, 3, 4, 0, 5, 0, 0, 5, 0, 0, 5, 4, 0, 0, 4, 0, 0, 3, 0, 0,
    5, 3, 0, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 2, 0, 0, 3, 0, 0,
    5, 2, 4, 3, 0, 0, 2, 0, 0, 4, 0, 0, 5, 2, 3, 0, 0, 5, 0, 0,
    4, 2, 3, 1, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 5, 4, 0, 0, 0,
    4, 0, 0, 5, 0, 0, 3, 0, 0, 0, 5, 0, 0, 1, 0, 0, 0, 1, 0, 0,
    5, 0, 0, 2, 3, 0, 3, 0, 1, 3, 0, 0, 3, 3, 0, 0, 5, 0, 5, 0,
    1, 0, 0, 4, 0, 2, 2, 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 0, 5, 0,
    4, 0, 0, 0, 0, 4, 5, 0, 0, 0, 0, 2, 1, 0, 3, 0, 4, 0, 0, 0,
    0, 2, 2, 0, 0, 0, 0, 4, 4, 1, 4, 3, 0, 0, 0, 5, 3, 0, 0, 0,
    3, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5,
    3, 5, 4, 1, 0, 0, 0, 5, 5, 5, 0, 0, 0, 1, 0, 5, 5, 0, 0, 4,
    0, 5, 0, 0, 0, 0, 5, 0, 0, 4, 0, 0, 2, 1, 1, 0, 0, 0, 0, 0,
    3, 4, 4, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 2, 5, 2, 0, 0, 0,
    0, 4, 2, 0, 0, 4, 0, 4, 0, 3, 0, 0, 0, 4, 0, 0, 4, 0, 0, 3,
    0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 5,
    0, 0, 0, 6, 2, 0, 0, 0, 6, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4,
    5, 4, 0, 0, 0, 2, 3, 0, 5, 0, 0, 0, 4, 0, 1, 4, 5, 5, 0, 0,
    0, 4, 1, 0, 0, 0, 3, 0, 0, 1, 0, 0, 0, 0, 4, 0, 0, 0, 5, 1,
    0, 0, 5, 4, 0, 0, 4, 2, 5, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0,
    5, 4, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0,
    0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 5, 0, 0,
    0, 0, 5, 0, 0, 0, 5, 4, 0, 0, 0, 1, 1, 2, 0, 0, 4, 4, 0, 0,
    0, 5, 0, 3, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 5,
    0, 0, 0, 0, 4, 0, 0, 0, 0, 3, 2, 4, 4, 3, 5, 3, 1, 3, 2, 0,
    0, 4, 4, 3, 3, 0, 0, 5, 4, 3, 0, 0, 0, 5, 0, 5, 0, 0, 0, 5,
    0, 0, 0, 4, 0, 5, 4, 0, 0, 0, 4, 2, 4, 4, 0, 3, 5, 0, 0, 5,
    5, 0, 0, 0, 2, 0, 0, 0, 0, 0, 3, 4, 0, 0, 0, 5, 3, 0, 0, 5,
    0, 0, 3, 0, 0, 3, 0, 0, 0, 0, 5, 4, 0, 1, 5, 0, 0, 5, 5, 5,
    3, 0, 0, 0, 2, 3, 0, 0, 2, 4, 0, 0, 0, 3, 0, 0, 4, 1, 3, 0,
    0, 2, 5, 0, 0, 0, 5, 0, 0, 5, 4, 5, 0, 0, 0, 3, 3, 4, 0, 0,
    4, 3, 2, 2, 0, 4, 3, 0, 0, 0, 3, 0, 1, 0, 5, 5, 0, 0, 0, 2,
    0, 0, 0, 0, 4, 4, 0, 0, 3, 4, 4, 4, 2, 1, 0, 0, 0, 3, 5, 0,
    0, 1, 0, 0, 3, 1, 4, 4, 4, 0, 5, 5, 0, 0, 0, 4, 0, 0, 0, 4,
    4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 5, 1, 0, 0, 5, 0, 0, 0, 2,
    2, 0, 0, 4, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 5, 5, 0, 0, 0, 3,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 4, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0,
    3, 0, 0, 6, 0, 1, 3, 0, 0, 0, 3, 0, 0, 4, 4, 0, 0, 0, 0, 3,
    0, 0, 0, 4, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 4, 0, 0,
    0, 5, 0, 0, 0, 5, 0, 0, 5, 0, 1, 0, 0, 0, 0, 5, 5, 5, 0, 0,
    0, 3, 5, 4, 3, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0, 0, 5, 0, 4, 0,
    0, 0, 0, 4, 5, 0, 0, 0, 3, 0, 0, 0, 0, 1, 1, 0, 0, 0, 5, 2,
    0, 0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 0, 0, 4, 4, 3,
    2, 2, 1, 0, 0, 0, 4, 0, 0, 5, 0, 5, 0, 2, 0, 3, 0, 4, 0, 0,
    0, 0, 3, 0, 2, 0, 0, 3, 0, 6, 0, 0, 0, 4, 0, 4, 0, 0, 0, 5,
    0, 4, 0, 4, 0, 0, 5, 5, 0, 0, 0, 4, 3, 0, 0, 2, 0, 3, 0, 0,
    4, 3, 0, 0, 3, 0, 0, 5, 0, 0, 0, 5, 4, 1, 0, 2, 1, 0, 0, 5,
    1, 0, 0, 0, 0, 4, 3, 4, 0, 0, 4, 4, 5, 4, 1, 0, 0, 0, 4, 1,
    2, 5, 2, 3, 4, 0, 0, 0, 0, 4, 4, 2, 2, 3, 0, 0, 2, 2, 0, 0,
    0, 3, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 4, 3, 0, 0, 0, 0, 1, 4,
    4, 5, 0, 0, 4, 0, 0, 0, 5, 5, 0, 1, 0, 5, 0, 1, 4, 0, 0, 5,
    5, 0, 2, 5, 2, 0, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 0, 0, 4,
    0, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 5, 0, 0, 0, 1, 0, 0, 2, 5,
    0, 0, 5, 2, 1, 4, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 0, 0, 4,
    0, 0, 0, 5, 5, 0, 0, 5, 0, 4, 5, 3, 0, 0, 0, 5, 0, 0, 5, 0,
    0, 4, 0, 0, 0, 0, 0, 5, 0, 0, 2, 1, 0, 0, 4, 0, 0, 3, 0, 0,
    0, 0, 0, 5, 0, 0, 3, 0, 0, 1, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0,
    2, 0, 3, 0, 4, 5, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 2, 0,
    2, 0, 0, 1, 0, 0, 0, 3, 0, 0, 2, 5, 0, 0, 0, 3, 0, 0, 3, 0,
    0, 4, 0, 0, 1, 0, 0, 2, 0, 0, 2, 0, 0, 1, 0, 0, 0, 3, 4, 4,
    1, 0, 4, 0, 3, 0, 4, 0, 0, 1, 0, 2, 0, 1, 2, 5, 1, 4, 3, 0,
    0, 0, 5, 0, 0, 0, 1, 0, 0, 4, 0, 4, 3, 3, 5, 0, 3, 0, 1, 0,
    0, 0, 5, 0, 0, 3, 0, 0, 4, 3, 0, 1, 0, 1, 1, 3, 5, 0, 0, 5,
    1, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 2, 0, 0, 1, 0, 0, 2, 0, 0,
    2, 0, 0, 0, 0, 5, 0, 0, 0, 1, 5, 0, 4, 0, 3, 0, 2, 1, 0, 1,
    0, 1, 0, 5, 2, 4, 5, 3, 2, 1, 3, 3, 4, 2, 1, 0, 2, 2, 3, 4,
    5, 2, 0, 1, 4, 1, 4, 1, 3, 0, 0, 5, 5, 2, 5, 0, 4, 0, 0, 4,
    0, 0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 4,
    0, 0, 0, 3, 0, 0, 0, 4, 0, 4, 3, 4, 5, 0, 0, 4, 3, 0, 5, 3,
    0, 1, 1, 0, 0, 0, 0, 5, 4, 0, 0, 0, 4, 0, 0, 0, 0, 5, 5, 0,
    0, 0, 0, 1, 5, 0, 0, 0, 4, 0, 0, 0, 0, 2, 1, 0, 0, 0, 0, 4,
    0, 1, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0, 3, 5, 0, 0, 0, 4, 4, 5,
    4, 2, 1, 2, 0, 0, 4, 0, 4, 0, 4, 0, 3, 0, 2, 1, 0, 1, 1, 0,
    0, 2, 0, 0, 2, 0, 1, 0, 0, 0, 5, 4, 4, 0, 0, 5, 0, 0, 4, 0,
    3, 0, 0, 0, 4, 0, 0, 4, 0, 0, 1, 0, 1, 1, 4, 0, 0, 4, 0, 1,
    0, 1, 0, 2, 5, 0, 0, 4, 0, 0, 0, 5, 0, 1, 4, 0, 3, 0, 0, 0,
    1, 0, 0, 0, 5, 0, 0, 4, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 0, 0,
    0, 3, 0, 0, 0, 3, 0, 5, 0, 0, 4, 3, 0, 0, 0, 3, 0, 0, 0, 4,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 2,
    0, 0, 0, 0, 3, 0, 0, 5, 0, 3, 0, 0, 0, 0, 3, 0, 0, 2, 0, 0,
    0, 5, 0, 0, 0, 0, 3, 0, 5, 0, 4, 3, 0, 0, 2, 0, 0, 0, 3, 0,
    0, 0, 5, 0, 0, 0, 5, 2, 1, 0, 4, 0, 4, 0, 4, 0, 4, 0, 3, 0,
    4, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 1, 0, 0, 5, 0, 3, 0, 0,
    2, 0, 4, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 0, 4, 0,
    5, 0, 4, 0, 4, 0, 2, 0, 4, 0, 4, 0, 4, 0, 2, 0, 0, 3, 0, 0,
    0, 5, 0, 1, 0, 4, 0, 0, 4, 0, 0, 4, 0, 1, 0, 4, 0, 0, 3, 2,
    2, 4, 0, 3, 0, 4, 1, 0, 0, 0, 4, 0, 0, 0, 1, 4, 0, 3, 0, 0,
    3, 0, 0, 3, 0, 0, 3, 0, 0, 1, 0, 0, 0, 5, 4, 0, 4, 0, 0, 0,
    0, 2, 0, 4, 0, 4, 0, 0, 1, 0, 0, 5, 0, 0, 2, 0, 0, 0, 3, 0,
    0, 2, 0, 0, 0, 3, 0, 0, 4, 0, 0, 2, 0, 0, 4, 0, 0, 4, 1, 0,
    0, 2, 4, 0, 0, 0, 0, 4, 0, 0, 4, 0, 5, 0, 1, 0, 4, 3, 1, 0,
    4, 5, 0, 0, 0, 0, 4, 0, 3, 0, 0, 5, 5, 3, 0, 0, 0, 0, 5, 0,
    4, 4, 0, 0, 0, 0, 4, 0, 0, 5, 0, 0, 4, 0, 0, 5, 0, 5, 0, 0,
    5, 0, 0, 5, 0, 4, 0, 4, 0, 0, 1, 0, 5, 5, 0, 1, 4, 0, 0, 0,
    0, 4, 0, 0, 2, 0, 5, 0, 0, 5, 0, 0, 0, 2, 0, 0, 5, 0, 3, 0,
    3, 0, 1, 0, 5, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 3, 0, 5, 0, 1,
    0, 4, 0, 0, 5, 0, 0, 0, 4, 0, 0, 4, 0, 1, 0, 4, 0, 4, 0, 0,
    0, 0, 4, 0, 0, 5, 0, 4, 0, 0, 0, 0, 0, 4, 0, 3, 0, 0, 5, 5,
    0, 4, 0, 0, 3, 0, 5, 0, 4, 1, 0, 1, 0, 0, 2, 0, 0, 2, 0, 3,
    0, 4, 5, 0, 0, 3, 0, 3, 0, 0, 5, 0, 5, 0, 4, 3, 0, 3, 0, 2,
    0, 0, 5, 0, 0, 0, 2, 1, 0, 3, 0, 0, 3, 4, 0, 0, 4, 0, 0, 0,
    0, 4, 0, 1, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 2, 0, 0, 4, 0, 5,
    0, 0, 5, 0, 4, 0, 4, 0, 5, 0, 0, 5, 0, 0, 5, 0, 1, 0, 2, 0,
    4, 0, 0, 3, 0, 0, 0, 4, 0, 0, 5, 0, 0, 1, 0, 2, 0, 0, 5, 0,
    0, 3, 3, 0, 4, 0, 0, 5, 0, 5, 0, 0, 3, 5, 0, 0, 0, 4, 0, 0,
    0, 0, 0, 5, 0, 0, 3, 0, 4, 0, 0, 5, 0, 0, 0, 3, 0, 3, 0, 0,
    5, 0, 4, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 1, 0, 5, 0, 5, 0, 0,
    3, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 3, 0, 0, 5, 0, 5,
    0, 5, 0, 3, 0, 0, 3, 0, 0, 3, 0, 5, 5, 4, 4, 0, 0, 0, 4, 0,
    4, 0, 0, 3, 0, 0, 4, 0, 0, 0, 5, 4, 0, 0, 3, 0, 0, 5, 0, 3,
    4, 0, 3, 0, 5, 0, 0, 2, 0, 0, 3, 0, 4, 0, 4, 0, 3, 0, 0, 0,
    3, 0, 1, 0, 0, 3, 0, 0, 5, 0, 0, 0, 4, 0, 4, 0, 0, 4, 0, 5,
    0, 5, 0, 2, 0, 3, 0, 0, 5, 0, 5, 0, 3, 0, 0, 5, 0, 0, 0, 0,
    4, 1, 6, 3, 3, 0, 0, 0, 1, 0, 4, 0, 1, 0, 4, 0, 0, 5, 0, 4,
    0, 0, 3, 0, 3, 0, 4, 0, 0, 4, 0, 0, 5, 0, 5, 0, 4, 0, 0, 5,
    0, 1, 0, 0, 0, 0, 3, 0, 4, 3, 0, 3, 0, 0, 1, 0, 0, 0, 4, 4,
    0, 0, 3, 4, 2, 0, 0, 4, 0, 0, 3, 0, 0, 4, 2, 0, 3, 0, 0, 0,
    5, 0, 0, 0, 3, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 5, 4, 0, 0,
    3, 0, 0, 0, 4, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 1, 0,
    0, 1, 0, 1, 0, 0, 4, 0, 2, 3, 4, 5, 0, 3, 0, 0, 0, 4, 0, 0,
    3, 0, 1, 4, 0, 0, 4, 0, 0, 0, 4, 0, 4, 0, 0, 3, 4, 0, 0, 4,
    4, 5, 0, 0, 3, 2, 0, 0, 5, 0, 5, 0, 0, 0, 4, 0, 0, 1, 0, 0,
    1, 0, 0, 3, 0, 0, 0, 3, 4, 0, 0, 3, 4, 0, 0, 0, 4, 5, 0, 1,
    4, 0, 4, 0, 4, 0, 0, 2, 0, 2, 0, 0, 5, 0, 3, 0, 0, 5, 0, 1,
    2, 0, 2, 0, 0, 5, 0, 0, 5, 0, 4, 0, 4, 0, 4, 4, 0, 3, 0, 0,
    0, 5, 0, 1, 0, 2, 0, 2, 0, 0, 5, 0, 0, 5, 0, 2, 5, 0, 0, 4,
    4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 5, 0, 0, 4, 0, 2, 0, 0, 3,
    0, 2, 0, 0, 5, 0, 1, 0, 3, 0, 0, 0, 6, 0, 0, 3, 0, 0, 5, 0,
    0, 4, 2, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 5,
    4, 0, 2, 0, 3, 2, 0, 5, 0, 0, 0, 0, 0, 0, 5, 0, 2, 0, 0, 5,
    0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 3, 0, 0, 0, 0, 0, 3,
    0, 0, 0, 1, 6, 1, 0, 0, 1, 0, 5, 0, 0, 0, 0, 4, 0, 0, 0, 4,
    0, 5, 0, 5, 0, 0, 5, 4, 0, 0, 5, 0, 3, 0, 3, 0, 0, 3, 0, 0,
    5, 0, 0, 3, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 5, 4, 5, 0, 2,
    0, 0, 0, 1, 0, 5, 0, 3, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 4,
    0, 0, 0, 0, 5, 0, 0, 5, 0, 0, 0, 2, 5, 0, 2, 0, 0, 5, 0, 5,
    0, 0, 3, 0, 0, 0, 0, 3, 0, 5, 0, 0, 0, 0, 4, 0, 0, 1, 0, 5,
    0, 1, 0, 0, 3, 0, 0, 0, 4, 0, 4, 0, 0, 0, 4, 0, 5, 0, 5, 0,
    1, 0, 4, 0, 5, 0, 3, 0, 0, 0, 0, 5, 0, 0, 3, 0, 3, 1, 5, 5,
    0, 0, 0, 4, 1, 0, 0, 3, 0, 0, 0, 3, 0, 0, 4, 4, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 5, 4, 0, 4, 0, 0, 0, 5, 5, 0, 0, 0, 5, 0, 0,
    3, 0, 0, 3, 4, 0, 0, 0, 0, 0, 5, 4, 5, 4, 4, 0, 0, 0, 0, 4,
    0, 0, 0, 0, 0, 3, 0, 0, 4, 0, 0, 0, 1, 4, 5, 2, 0, 0, 3, 0,
    0, 4, 0, 0, 0, 2, 0, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    1, 5, 0, 0, 0, 4, 4, 1, 0, 4, 0, 4, 0, 5, 0, 2, 5, 0, 4, 0,
    2, 4, 1, 0, 0, 3, 2, 3, 4, 3, 0, 3, 4, 0, 3, 0, 0, 3, 3, 0,
    0, 0, 4, 5, 5, 4, 0, 0, 3, 0, 0, 5, 0, 0, 0, 0, 0, 5, 0, 0,
    0, 5, 0, 0, 0, 0, 5, 4, 2, 5, 0, 0, 0, 2, 5, 0, 0, 2, 0, 1,
    0, 3, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 4, 2, 0, 0, 0,
    1, 6, 0, 5, 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 0, 2, 3, 0,
    2, 0, 5, 4, 4, 1, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 2, 0, 0,
    5, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 5, 0,
    0, 0, 5, 4, 5, 0, 0, 0, 4, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0, 2,
    0, 0, 0, 0, 4, 0, 0, 0, 0, 5, 2, 3, 2, 0, 4, 0, 2, 3, 0, 0,
    5, 4, 0, 0, 4, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 4,
    4, 1, 1, 0, 0, 0, 4, 5, 3, 0, 0, 3, 2, 0, 0, 5, 0, 5, 0, 0,
    0, 5, 0, 0, 3, 0, 0, 0, 0, 5, 4, 0, 0, 0, 5, 4, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 3, 4, 4, 0, 3, 0, 0, 4, 2, 2, 0, 0, 0, 0, 4,
    0, 0, 0, 4, 0, 0, 5, 0, 0, 5, 4, 1, 0, 0, 4, 0, 0, 0, 2, 0,
    0, 5, 4, 4, 1, 0, 0, 3, 1, 0, 0, 3, 0, 4, 5, 0, 0, 0, 0, 5,
    0, 0, 0, 2, 4, 0, 0, 4, 0, 0, 4, 4, 1, 2, 0, 2, 0, 3, 0, 0,
    0, 0, 5, 0, 0, 0, 4, 0, 0, 3, 0, 0, 5, 0, 0, 4, 0, 0, 0, 1,
    5, 1, 0, 4, 0, 0, 0, 5, 0, 1, 5, 0, 4, 0, 0, 0, 0, 5, 5, 0,
    0, 0, 4, 0, 3, 3, 4, 0, 0, 0, 5, 5, 4, 5, 3, 0, 0, 0, 4, 0,
    3, 0, 0, 4, 5, 3, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 0, 5, 5, 1,
    0, 1, 0, 0, 0, 4, 0, 3, 4, 1, 3, 5, 0, 0, 5, 0, 0, 0, 3, 0,
    1, 0, 4, 0, 0, 4, 0, 4, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 4,
    0, 1, 0, 2, 0, 4, 0, 1, 0, 0, 0, 5, 0, 4, 0, 2, 1, 0, 2, 1,
    3, 0, 0, 0, 5, 5, 3, 4, 4, 0, 0, 3, 0, 0, 0, 2, 4, 3, 3, 0,
    0, 0, 0, 0, 5, 0, 0, 3, 2, 1, 4, 3, 1, 5, 0, 0, 5, 0, 0, 0,
    5, 5, 0, 0, 0, 1, 0, 3, 1, 4, 0, 4, 0, 0, 0, 0, 2, 5, 0, 5,
    0, 0, 0, 0, 0, 5, 5, 4, 4, 0, 0, 0, 4, 4, 0, 0, 0, 0, 0, 1,
    4, 0, 4, 0, 0, 0, 0, 4, 0, 0, 2, 0, 5, 0, 0, 0, 3, 0, 0, 4,
    3, 2, 5, 0, 0, 5, 3, 3, 4, 0, 4, 0, 1, 0, 3, 1, 2, 5, 3, 0,
    0, 5, 0, 3, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0,
    5, 0, 0, 0, 0, 1, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 4, 0, 0, 0,
    4, 0, 0, 0, 4, 5, 0, 0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5,
    0, 5, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 2, 3, 0, 5, 0, 0, 0,
    3, 0, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 0, 3,
    0, 0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 4, 5, 0, 0, 0, 3, 0, 1, 1, 3, 0, 0, 4,
    0, 5, 0, 4, 0, 0, 5, 0, 5, 0, 0, 3, 4, 0, 0, 0, 4, 0, 0, 0,
    4, 0, 5, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 4, 0, 0, 0, 5, 0,
    0, 0, 5, 0, 0, 0, 5, 0, 4, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0,
    0, 0, 5, 0, 3, 0, 5, 0, 3, 0, 1, 0, 0, 2, 5, 0, 0, 4, 0, 0,
    0, 4, 0, 0, 0, 4, 0, 0, 0, 2, 3, 0, 1, 0, 1, 0, 0, 5, 0, 0,
    4, 0, 0, 0, 0, 5, 0, 4, 0, 2, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0,
    0, 0, 4, 0, 0, 4, 0, 3, 5, 0, 0, 1, 4, 0, 0, 0, 4, 0, 0, 3,
    0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0,
    3, 0, 0, 0, 0, 5, 0, 0, 2, 4, 4, 1, 4, 0, 0, 0, 0, 4, 0, 2,
    0, 0, 0, 3, 4, 1, 0, 0, 0, 0, 4, 2, 1, 0, 0, 0, 0, 3, 0, 5,
    0, 5, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 5, 3, 0, 0, 4, 0, 0,
    4, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 5, 3, 0, 0, 0, 0, 4, 0,
    0, 0, 5, 0, 0, 6, 1, 0, 0, 5, 0, 0, 0, 0, 3, 0, 0, 5, 0, 0,
    0, 4, 0, 0, 5, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 3, 0, 0, 0, 5,
    4, 5, 4, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 3, 4, 1,
    2, 0, 4, 0, 4, 0, 0, 1, 0, 2, 1, 0, 0, 5, 0, 4, 0, 0, 4, 0,
    0, 4, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 3, 4, 0, 0, 4, 0, 1,
    0, 4, 0, 0, 3, 0, 0, 3, 0, 0, 0, 0, 1, 0, 0, 2, 2, 1, 0, 2,
    0, 0, 0, 4, 0, 0, 0, 5, 0, 2, 4, 0, 0, 0, 3, 4, 0, 0, 4, 0,
    0, 5, 0, 0, 0, 0, 4, 0, 4, 0, 0, 4, 0, 4, 0, 0, 0, 4, 0, 0,
    3, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 5, 0, 1, 0, 0,
    3, 0, 5, 0, 1, 0, 2, 5, 0, 5, 4, 5, 4, 0, 4, 0, 4, 0, 0, 0,
    0, 5, 0, 4, 0, 0, 0, 0, 4, 4, 4, 2, 0, 5, 0, 0, 5, 0, 2, 0,
    0, 3, 0, 4, 0, 2, 5, 4, 1, 5, 0, 4, 0, 0, 4, 0, 0, 0, 0, 2,
    0, 0, 4, 3, 0, 0, 3, 0, 0, 4, 0, 0, 5, 0, 3, 2, 0, 4, 0, 0,
    5, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 4, 0, 2, 0, 0, 5, 0, 0, 0,
    4, 0, 5, 0, 0, 3, 0, 0, 4, 0, 0, 0, 5, 0, 0, 1, 0, 0, 5, 0,
    3, 0, 4, 0, 0, 3, 0, 4, 0, 2, 0, 0, 5, 2, 0, 4, 0, 0, 0, 4,
    5, 0, 5, 0, 0, 0, 0, 3, 0, 0, 0, 5, 4, 0, 0, 0, 4, 0, 5, 0,
    3, 0, 1, 0, 3, 0, 1, 0, 3, 4, 0, 0, 0, 0, 1, 0, 0, 5, 0, 0,
    0, 5, 0, 0, 4, 4, 0, 3, 0, 3, 4, 2, 0, 0, 0, 5, 0, 0, 3, 0,
    0, 0, 0, 3, 4, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 4, 4, 0,
    0, 0, 4, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 3, 0, 0, 5, 0, 5, 0,
    0, 0, 5, 0, 0, 1, 0, 3, 4, 5, 4, 0, 3, 0, 4, 4, 0, 1, 0, 0,
    3, 4, 0, 4, 0, 2, 5, 0, 0, 0, 5, 0, 3, 0, 0, 1, 0, 0, 0, 0,
    4, 0, 0, 5, 0, 0, 1, 0, 0, 3, 0, 0, 2, 0, 0, 3, 0, 0, 4, 2,
    0, 0, 2, 0, 0, 0, 5, 4, 0, 0, 3, 0, 0, 4, 0, 0, 5, 0, 0, 3,
    0, 4, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 5, 4, 0, 0, 1,
    0, 0, 5, 0, 0, 0, 5, 0, 5, 4, 0, 0, 4, 0, 3, 0, 0, 3, 0, 0,
    3, 0, 2, 0, 0, 3, 2, 0, 4, 3, 4, 0, 0, 0, 0, 0, 4, 0, 0, 3,
    4, 0, 0, 5, 2, 0, 3, 0, 0, 0, 0, 4, 0, 5, 0, 0, 0, 0, 0, 1,
    2, 4, 4, 0, 0, 5, 4, 0, 0, 5, 4, 4, 2, 0, 5, 0, 4, 0, 0, 3,
    0, 0, 1, 0, 5, 0, 5, 5, 0, 0, 3, 4, 4, 2, 2, 1, 0, 4, 0, 4,
    0, 0, 0, 4, 0, 4, 2, 0, 0, 3, 0, 0, 0, 0, 0, 5, 2, 2, 0, 4,
    0, 0, 1, 0, 5, 4, 2, 4, 0, 0, 0, 0, 4, 0, 0, 2, 0, 1, 0, 0,
    4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 5, 0, 0, 0, 3,
    0, 4, 0, 0, 5, 0, 5, 0, 0, 4, 0, 4, 2, 0, 0, 0, 4, 0, 0, 0,
    0, 0, 0, 4, 0, 0, 3, 0, 0, 4, 0, 0, 4, 0, 4, 0, 0, 3, 0, 3,
    0, 0, 5, 0, 0, 3, 0, 0, 3, 0, 3, 4, 0, 1, 0, 0, 0, 4, 0, 4,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 4, 0, 4, 0, 0, 4, 0, 4, 0, 0, 5,
    0, 0, 1, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 3, 5, 5, 2, 0, 0, 4,
    0, 0, 0, 4, 5, 0, 0, 0, 0, 6, 3, 0, 0, 5, 2, 0, 0, 5, 0, 0,
    3, 0, 0, 0, 0, 5, 2, 0, 1, 0, 0, 3, 4, 0, 0, 3, 3, 0, 0, 5,
    0, 0, 3, 0, 0, 0, 5, 0, 0, 3, 0, 0, 0, 4, 0, 5, 0, 0, 5, 4,
    0, 4, 0, 0, 0, 0, 0, 4, 4, 0, 2, 0, 0, 0, 5, 0, 0, 1, 0, 0,
    2, 0, 0, 4, 4, 0, 1, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0,
    0, 4, 0, 0, 1, 0, 0, 1, 0, 0, 0, 4, 4, 0, 2, 0, 0, 5, 4, 0,
    0, 0, 4, 0, 4, 4, 0, 0, 5, 0, 3, 0, 3, 2, 0, 0, 3, 0, 5, 0,
    0, 4, 0, 1, 2, 0, 0, 0, 5, 2, 0, 1, 4, 4, 2, 0, 0, 3, 5, 5,
    0, 0, 3, 0, 0, 5, 0, 2, 0, 0, 0, 0, 5, 0, 5, 2, 4, 0, 4, 0,
    2, 5, 5, 4, 0, 4, 0, 0, 5, 4, 0, 0, 3, 0, 5, 0, 0, 3, 4, 0,
    1, 2, 0, 0, 3, 0, 0, 3, 0, 4, 3, 0, 4, 0, 0, 5, 0, 0, 5, 0,
    0, 1, 0, 5, 0, 0, 3, 3, 0, 4, 3, 4, 5, 0, 0, 4, 4, 4, 0, 0,
    0, 4, 5, 5, 0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 4, 0, 0, 0, 0,
    0, 1, 2, 1, 0, 0, 1, 0, 1, 0, 0, 0, 5, 4, 4, 0, 0, 0, 3, 0,
    0, 4, 5, 3, 0, 3, 0, 5, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 1, 0,
    2, 1, 0, 0, 4, 0, 0, 5, 1, 0, 6, 0, 3, 0, 4, 0, 1, 0, 0, 0,
    4, 0, 3, 0, 0, 4, 0, 3, 0, 0, 4, 0, 1, 5, 5, 2, 0, 4, 0, 0,
    0, 0, 5, 0, 4, 0, 4, 0, 0, 0, 4, 0, 5, 1, 0, 5, 0, 0, 0, 4,
    0, 0, 4, 0, 0, 0, 4, 0, 5, 0, 0, 4, 0, 1, 4, 4, 0, 1, 0, 5,
    1, 2, 0, 0, 5, 0, 0, 0, 0, 4, 0, 3, 1, 0, 0, 0, 5, 4, 1, 2,
    0, 4, 0, 3, 0, 0, 4, 0, 4, 0, 5, 0, 1, 0, 0, 0, 3, 0, 4, 0,
    0, 0, 0, 4, 0, 0, 4, 3, 5, 0, 4, 0, 0, 3, 0, 0, 0, 4, 0, 0,
    0, 6, 3, 0, 0, 0, 3, 3, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 4,
    0, 0, 0, 5, 0, 1, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0,
    5, 4, 4, 4, 0, 0, 4, 4, 2, 1, 0, 0, 0, 0, 4, 4, 1, 2, 0, 0,
    0, 4, 0, 3, 0, 3, 0, 0, 0, 6, 1, 0, 3, 2, 0, 2, 0, 0, 4, 0,
    0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 3, 0, 4, 0, 0, 2, 0, 0, 0, 0,
    6, 0, 0, 0, 4, 1, 0, 0, 4, 1, 0, 0, 3, 0, 6, 0, 0, 0, 0, 5,
    5, 0, 0, 3, 6, 0, 2, 5, 0, 0, 4, 0, 0, 0, 5, 4, 3, 5, 1, 0,
    1, 1, 0, 0, 3, 0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 0, 4, 3, 3, 4,
    0, 4, 0, 0, 0, 2, 0, 0, 5, 5, 3, 5, 0, 3, 0, 0, 0, 4, 0, 0,
    0, 4, 0, 0, 0, 4, 3, 4, 2, 0, 5, 4, 1, 4, 0, 5, 0, 0, 0, 0,
    3, 0, 4, 0, 0, 0, 3, 2, 3, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 5,
    0, 0, 4, 0, 0, 4, 0, 0, 5, 5, 0, 0, 4, 4, 4, 0, 4, 0, 3, 0,
    3, 0, 0, 0, 5, 3, 0, 0, 0, 3, 0, 4, 0, 0, 4, 5, 3, 0, 0, 4,
    3, 4, 4, 4, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 4, 0, 4, 0, 4, 0,
    4, 1, 4, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 5, 0, 0, 5, 4,
    4, 0, 0, 0, 4, 0, 1, 0, 2, 0, 0, 0, 0, 1, 5, 5, 0, 5, 5, 0,
    0, 0, 3, 0, 1, 4, 0, 0, 0, 3, 0, 3, 0, 0, 0, 4, 0, 1, 0, 4,
    0, 2, 0, 5, 0, 3, 0, 3, 0, 3, 0, 3, 0, 4, 0, 3, 0, 4, 0, 0,
    1, 0, 0, 2, 0, 2, 0, 0, 4, 0, 5, 0, 2, 3, 0, 0, 4, 0, 0, 0,
    0, 0, 0, 5, 0, 0, 5, 0, 5, 2, 0, 5, 0, 0, 3, 0, 4, 0, 0, 0,
    0, 4, 0, 5, 0, 3, 2, 1, 2, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 6,
    0, 0, 0, 5, 0, 0, 1, 0, 0, 1, 1, 0, 0, 4, 4, 0, 0, 2, 3, 0,
    5, 3, 0, 0, 0, 3, 5, 0, 0, 0, 4, 0, 3, 3, 0, 0, 0, 0, 5, 5,
    0, 0, 0, 3, 0, 3, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 5, 0, 0, 0,
    5, 5, 0, 0, 0, 5, 5, 5, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 5,
    2, 4, 2, 0, 0, 0, 5, 0, 3, 0, 5, 0, 0, 5, 0, 3, 0, 4, 0, 5,
    4, 1, 2, 1, 2, 0, 4, 0, 2, 0, 4, 4, 0, 0, 5, 0, 0, 0, 0, 0,
    5, 0, 1, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 3,
    0, 0, 0, 5, 2, 0, 5, 0, 0, 0, 0, 4, 0, 1, 0, 0, 0, 2, 0, 0,
    0, 0, 3, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 3,
    0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 5, 0, 0, 4, 1, 0, 0, 5, 5, 0,
    5, 5, 1, 0, 0, 0, 3, 0, 0, 3, 0, 0, 0, 3, 4, 0, 0, 0, 0, 4,
    0, 0, 0, 3, 1, 0, 5, 0, 5, 0, 0, 4, 2, 1, 1, 4, 4, 3, 0, 0,
    1, 1, 0, 0, 5, 0, 0, 3, 2, 0, 0, 4, 0, 5, 0, 0, 5, 1, 2, 0,
    0, 2, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 5, 2, 0, 0, 0, 0, 5, 0,
    0, 1, 0, 1, 4, 0, 0, 3, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0,
    2, 5, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 3, 1, 4, 5, 0, 0, 0,
    1, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 5, 0, 0, 5, 0, 0, 0, 4,
    0, 0, 0, 3, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 1, 5, 0, 0, 0, 0,
    3, 0, 0, 3, 4, 4, 1, 0, 0, 0, 4, 5, 0, 5, 0, 4, 3, 0, 0, 0,
    4, 4, 5, 4, 2, 4, 5, 0, 0, 0, 0, 2, 0, 0, 0, 3, 3, 0, 0, 3,
    0, 0, 0, 0, 0, 6, 1, 0, 5, 5, 0, 0, 2, 0, 0, 3, 0, 0, 0, 3,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 1, 3, 1, 0, 2, 0,
    0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 4, 4, 0, 0, 0, 4, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 5, 3, 0, 0, 0, 4, 0, 0, 5, 0, 5, 2, 3, 0,
    0, 4, 0, 0, 0, 3, 0, 0, 1, 0, 0, 5, 0, 4, 5, 0, 0, 5, 0, 0,
    3, 0, 0, 4, 4, 1, 2, 5, 0, 0, 3, 0, 0, 0, 4, 3, 0, 3, 0, 5,
    0, 0, 0, 4, 0, 0, 0, 4, 3, 0, 5, 5, 3, 0, 4, 0, 0, 0, 2, 1,
    0, 0, 0, 5, 0, 5, 0, 0, 0, 4, 3, 0, 5, 0, 4, 0, 0, 0, 5, 0,
    5, 0, 0, 6, 3, 0, 0, 0, 4, 0, 4, 0, 0, 1, 2, 0, 1, 0, 0, 1,
    2, 0, 1, 0, 0, 0, 0, 4, 0, 2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0,
    0, 4, 0, 5, 4, 0, 2, 4, 4, 1, 0, 1, 0, 3, 0, 1, 0, 0, 0, 5,
    4, 1, 0, 0, 4, 0, 4, 0, 0, 4, 1, 4, 5, 0, 0, 1, 0, 1, 5, 0,
    0, 2, 1, 0, 0, 5, 2, 0, 0, 4, 0, 0, 0, 0, 2, 0, 0, 0, 5, 4,
    0, 0, 0, 1, 5, 0, 0, 5, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 4, 0,
    1, 0, 0, 0, 5, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0,
    3, 0, 0, 0, 0, 3, 0, 0, 4, 0, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0,
    0, 1, 6, 0, 0, 3, 0, 0, 2, 4, 0, 0, 0, 0, 6, 5, 0, 0, 0, 2,
    0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 2, 0, 5,
    3, 0, 1, 0, 3, 0, 0, 2, 4, 1, 0, 0, 0, 0, 0, 5, 0, 0, 0, 5,
    0, 0, 0, 0, 5, 0, 3, 0, 0, 0, 0, 0, 4, 0, 2, 0, 0, 0, 4, 0,
    0, 5, 0, 4, 1, 0, 5, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 0, 5, 0,
    4, 0, 0, 0, 0, 5, 0, 0, 4, 0, 2, 4, 3, 4, 1, 2, 0, 4, 0, 5,
    0, 0, 0, 0, 2, 0, 5, 4, 1, 0, 0, 2, 0, 0, 0, 0, 5, 4, 5, 0,
    0, 0, 0, 0, 3, 0, 0, 0, 2, 0, 5, 3, 0, 0, 0, 2, 4, 0, 0, 4,
    4, 1, 2, 1, 2, 0, 4, 4, 0, 0, 4, 0, 5, 0, 0, 0, 5, 0, 0, 0,
    4, 0, 0, 4, 0, 0, 5, 4, 0, 0, 5, 0, 2, 0, 0, 0, 0, 0, 4, 0,
    0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 3, 0, 4, 4, 0, 0, 0,
    3, 0, 0, 0, 4, 0, 5, 0, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 2, 0,
    0, 3, 0, 0, 0, 0, 5, 0, 2, 0, 0, 0, 3, 3, 0, 0, 0, 4, 4, 1,
    4, 0, 0, 0, 0, 5, 0, 4, 0, 3, 0, 5, 0, 3, 0, 2, 0, 1, 0, 5,
    0, 3, 0, 0, 1, 0, 0, 4, 0, 0, 0, 0, 0, 5, 0, 1, 0, 1, 0, 4,
    0, 5, 0, 1, 0, 0, 5, 0, 0, 0, 4, 0, 1, 0, 5, 2, 0, 1, 0, 3,
    0, 0, 3, 0, 5, 0, 0, 0, 4, 0, 0, 2, 2, 0, 3, 0, 0, 2, 0, 0,
    3, 0, 0, 0, 3, 0, 0, 2, 5, 2, 0, 0, 4, 0, 0, 0, 5, 5, 0, 0,
    4, 0, 0, 0, 5, 0, 0, 5, 0, 0, 4, 1, 4, 3, 0, 0, 4, 0, 0, 2,
    0, 1, 0, 0, 0, 0, 5, 0, 4, 0, 2, 0, 4, 0, 5, 0, 0, 0, 4, 1,
    2, 2, 0, 1, 4, 2, 4, 3, 0, 0, 4, 0, 5, 0, 0, 4, 0, 3, 0, 0,
    0, 6, 3, 0, 4, 0, 3, 0, 0, 0, 4, 4, 0, 0, 0, 5, 0, 5, 0, 3,
    0, 0, 5, 0, 0, 1, 0, 3, 0, 0, 1, 0, 5, 0, 4, 0, 0, 0, 0, 4,
    0, 0, 5, 0, 0, 1, 0, 1, 0, 4, 0, 2, 4, 1, 4, 0, 0, 0, 4, 0,
    0, 0, 0, 3, 0, 0, 0, 4, 3, 4, 0, 0, 3, 4, 5, 4, 0, 0, 4, 0,
    0, 3, 0, 0, 4, 0, 0, 4, 0, 0, 5, 0, 0, 4, 0, 0, 2, 0, 0, 5,
    0, 5, 0, 0, 0, 4, 0, 1, 0, 0, 3, 0, 1, 5, 0, 0, 0, 4, 0, 0,
    4, 5, 0, 0, 0, 4, 0, 2, 0, 4, 3, 0, 3, 0, 0, 3, 0, 1, 4, 0,
    2, 0, 5, 0, 3, 0, 0, 3, 0, 1, 0, 0, 3, 4, 1, 0, 5, 0, 5, 0,
    0, 0, 4, 0, 0, 0, 0, 4, 4, 1, 2, 0, 0, 0, 4, 0, 0, 0, 3, 0,
    0, 0, 4, 0, 0, 0, 4, 0, 0, 3, 5, 0, 0, 3, 0, 1, 0, 5, 4, 3,
    2, 3, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 5, 0, 0, 5, 4, 5, 0, 0,
    0, 1, 6, 0, 0, 0, 5, 1, 3, 0, 1, 1, 0, 5, 5, 0, 0, 4, 0, 0,
    3, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 1,
    4, 0, 5, 4, 5, 0, 0, 5, 5, 5, 0, 0, 1, 0, 0, 0, 5, 0, 0, 4,
    4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 5, 1, 3, 0, 0, 0, 3, 0, 3,
    0, 0, 1, 0, 0, 0, 0, 3, 0, 1, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4,
    0, 1, 0, 1, 0, 0, 0, 4, 2, 1, 2, 0, 0, 5, 0, 0, 0, 0, 0, 4,
    0, 0, 4, 0, 0, 5, 0, 2, 0, 4, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    4, 0, 2, 0, 0, 3, 0, 0, 4, 0, 4, 0, 0, 4, 0, 5, 0, 0, 0, 0,
    5, 0, 1, 0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 2, 0, 5, 0, 0, 0,
    4, 0, 0, 0, 2, 0, 3, 0, 4, 3, 0, 0, 0, 4, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 3, 0, 0, 1, 0, 0, 4, 0, 0,
    5, 0, 0, 0, 4, 0, 3, 3, 0, 3, 0, 4, 0, 0, 1, 0, 5, 3, 0, 4,
    0, 3, 0, 0, 3, 0, 1, 2, 0, 1, 4, 0, 0, 0, 4, 0, 0, 0, 4, 4,
    0, 3, 4, 0, 0, 0, 3, 0, 5, 5, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0,
    0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 3, 3, 0, 5, 0, 0, 0, 4, 0, 1,
    0, 2, 0, 0, 5, 0, 0, 0, 0, 2, 1, 0, 3, 0, 0, 3, 0, 1, 0, 0,
    0, 4, 0, 3, 0, 3, 0, 0, 0, 0, 3, 0, 4, 0, 4, 0, 4, 0, 0, 3,
    0, 0, 5, 0, 0, 0, 0, 3, 5, 0, 0, 0, 0, 0, 5, 0, 0, 3, 0, 5,
    0, 0, 5, 0, 0, 0, 0, 3, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 0, 1,
    0, 2, 0, 4, 0, 0, 0, 0, 3, 0, 0, 5, 0, 0, 5, 0, 4, 0, 5, 0,
    3, 0, 0, 0, 0, 0, 2, 0, 0, 4, 0, 3, 0, 2, 0, 0, 5, 0, 0, 0,
    0, 4, 0, 2, 5, 5, 0, 0, 5, 0, 4, 0, 1, 0, 5, 0, 5, 0, 4, 0,
    3, 1, 1, 0, 0, 3, 0, 4, 0, 5, 2, 3, 0, 4, 0, 0, 0, 5, 1, 2,
    0, 1, 2, 0, 0, 0, 0, 5, 0, 0, 2, 0, 0, 0, 3, 0, 0, 3, 0, 0,
    0, 0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 3, 0, 0, 0, 0, 5, 0, 5, 0,
    0, 5, 0, 0, 0, 0, 5, 0, 0, 3, 0, 5, 2, 0, 3, 0, 0, 0, 1, 0,
    0, 5, 0, 1, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 1, 0,
    0, 3, 0, 3, 0, 3, 0, 0, 3, 0, 3, 0, 0, 4, 0, 0, 2, 0, 3, 0,
    3, 0, 0, 5, 0, 3, 0, 4, 0, 0, 0, 3, 0, 0, 1, 1, 0, 5, 0, 0,
    3, 0, 5, 0, 5, 0, 0, 3, 0, 5, 0, 5, 0, 5, 0, 0, 0, 0, 4, 0,
    0, 5, 0, 1, 0, 3, 4, 0, 0, 0, 4, 0, 0, 5, 0, 0, 2, 0, 0, 3,
    0, 0, 3, 0, 0, 3, 0, 5, 0, 0, 2, 0, 2, 0, 0, 5, 0, 0, 0, 5,
    0, 0, 5, 0, 0, 2, 0, 0, 4, 0, 2, 0, 3, 0, 0, 0, 0, 6, 0, 0,
    3, 0, 0, 5, 0, 4, 0, 0, 5, 0, 3, 0, 0, 3, 0, 0, 3, 0, 5, 0,
    0, 1, 0, 5, 5, 0, 0, 0, 4, 0, 4, 0, 0, 3, 0, 0, 0, 0, 0, 5,
    0, 2, 0, 0, 1, 0, 0, 4, 0, 3, 0, 0, 1, 3, 2, 0, 0, 5, 0, 3,
    0, 0, 5, 0, 0, 4, 0, 0, 1, 0, 3, 0, 0, 1, 0, 5, 0, 0, 3, 0,
    0, 4, 0, 0, 0, 0, 1, 0, 0, 3, 0, 3, 0, 3, 0, 2, 1, 3, 0, 3,
    0, 0, 3, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0,
    4, 0, 0, 0, 0, 0, 4, 0, 0, 3, 4, 0, 0, 0, 0, 0, 5, 0, 0, 5,
    0, 0, 0, 0, 5, 0, 0, 2, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 4, 0,
    0, 0, 3, 0, 3, 0, 0, 0, 0, 5, 0, 2, 0, 0, 0, 5, 0, 0, 1, 3,
    4, 2, 0, 5, 0, 5, 0, 0, 3, 0, 0, 1, 0, 3, 0, 5, 0, 4, 0, 1,
    0, 0, 1, 0, 0, 0, 5, 0, 1, 0, 1, 0, 5, 0, 4, 3, 0, 0, 5, 0,
    0, 5, 0, 0, 0, 5, 0, 5, 0, 0, 3, 0, 0, 0, 5, 0, 0, 5, 0, 0,
    0, 0, 4, 0, 0, 4, 4, 5, 0, 0, 3, 0, 5, 0, 0, 1, 0, 1, 0, 0,
    3, 0, 3, 0, 0, 2, 0, 0, 0, 2, 0, 5, 0, 0, 3, 0, 0, 5, 3, 0,
    0, 4, 0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 0, 3, 0, 0, 4, 0, 0, 3,
    0, 4, 0, 0, 0, 0, 1, 0, 0, 3, 0, 0, 5, 0, 0, 4, 0, 5, 0, 1,
    0, 0, 3, 0, 0, 2, 0, 0, 4, 0, 3, 4, 0, 5, 0, 0, 4, 4, 0, 0,
    5, 0, 0, 3, 0, 0, 3, 0, 0, 0, 4, 0, 0, 4, 0, 2, 0, 0, 4, 0,
    4, 3, 0, 0, 4, 0, 0, 2, 0, 5, 0, 0, 5, 0, 0, 5, 0, 4, 0, 0,
    0, 0, 0, 4, 0, 0, 3, 0, 0, 5, 0, 4, 0, 5, 0, 0, 0, 1, 0, 5,
    0, 0, 3, 4, 0, 0, 0, 3, 1, 1, 0, 0, 0, 5, 0, 0, 0, 3, 4, 0,
    0, 3, 0, 0, 5, 0, 3, 0, 3, 0, 3, 0, 0, 0, 5, 0, 0, 2, 0, 0,
    3, 0, 3, 0, 0, 3, 0, 0, 0, 0, 5, 0, 0, 5, 0, 0, 4, 0, 0, 0,
    0, 5, 0, 0, 0, 2, 0, 0, 5, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0,
    0, 3, 0, 0, 4, 0, 6, 3, 4, 0, 3, 0, 0, 0, 0, 0, 4, 0, 5, 4,
    0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 1, 0, 0, 0, 5, 0, 4, 0, 0,
    0, 6, 1, 0, 0, 1, 1, 0, 0, 4, 0, 0, 4, 0, 0, 0, 4, 0, 4, 5,
    0, 3, 0, 4, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 6, 0, 0, 0, 4, 0,
    0, 0, 3, 0, 0, 0, 4, 0, 0, 3, 0, 0, 1, 0, 0, 4, 0, 0, 0, 0,
    5, 0, 0, 0, 5, 0, 0, 0, 0, 0, 6, 0, 4, 0, 0, 0, 4, 1, 0, 0,
    0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 0, 5, 3, 0, 0, 0, 5, 0, 4,
    4, 0, 0, 0, 4, 0, 0, 2, 0, 0, 5, 5, 0, 0, 5, 0, 0, 4, 0, 0,
    0, 4, 3, 4, 1, 0, 0, 4, 4, 3, 0, 4, 0, 0, 0, 0, 4, 0, 0, 2,
    2, 2, 3, 3, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 4, 0, 0, 0, 4,
    0, 0, 0, 2, 1, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 4,
    0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 4, 0, 0, 0, 0, 5, 0, 4, 0, 4,
    0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 0, 5, 0, 4, 0, 0, 0,
    3, 0, 0, 0, 3, 0, 0, 5, 0, 0, 0, 1, 0, 0, 2, 0, 0, 5, 0, 0,
    5, 4, 4, 4, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 4, 0, 0, 4, 0,
    0, 1, 5, 0, 0, 0, 2, 3, 0, 0, 0, 5, 0, 0, 1, 3, 0, 0, 5, 5,
    3, 0, 0, 2, 3, 3, 5, 0, 0, 0, 4, 4, 0, 0, 3, 5, 1, 0, 0, 3,
    0, 0, 0, 0, 4, 0, 0, 2, 1, 3, 0, 0, 4, 0, 0, 4, 0, 4, 0, 5,
    0, 0, 3, 5, 3, 0, 0, 3, 0, 0, 4, 0, 0, 3, 0, 0, 2, 0, 4, 0,
    0, 0, 0, 4, 0, 4, 3, 0, 1, 0, 0, 0, 0, 4, 0, 3, 0, 0, 5, 0,
    0, 2, 2, 3, 2, 1, 2, 2, 3, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0,
    0, 2, 1, 0, 0, 0, 5, 4, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 0,
    4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 4, 1, 2, 3, 0, 0, 4, 5, 0, 0,
    5, 0, 0, 3, 0, 5, 5, 0, 4, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 5,
    1, 0, 0, 0, 0, 1, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0, 5, 0, 0,
    4, 0, 0, 4, 1, 4, 0, 0, 4, 1, 0, 0, 0, 1, 0, 4, 0, 0, 4, 5,
    4, 1, 0, 0, 0, 5, 0, 2, 0, 4, 0, 5, 0, 3, 0, 3, 0, 3, 0, 0,
    0, 5, 0, 0, 2, 0, 0, 0, 0, 4, 5, 0, 0, 0, 5, 0, 0, 0, 3, 0,
    0, 0, 0, 5, 0, 0, 0, 4, 1, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0,
    0, 3, 0, 3, 0, 0, 0, 0, 2, 3, 0, 0, 0, 5, 0, 0, 0, 3, 5, 0,
    0, 4, 0, 0, 0, 0, 4, 3, 0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 3, 0,
    0, 0, 2, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 5, 0, 0, 0, 2, 1, 0,
    0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 1, 2, 1, 2, 0, 2, 3, 0, 0,
    0, 2, 0, 0, 0, 0, 3, 6, 3, 2, 0, 0, 0, 0, 3, 6, 3, 2, 0, 0,
    4, 0, 4, 2, 1, 0, 0, 5, 4, 0, 2, 0, 2, 0, 0, 0, 3, 0, 0, 0,
    3, 0, 0, 4, 0, 5, 0, 0, 0, 3, 0, 0, 0, 4, 0, 4, 0, 0, 0, 3,
    0, 0, 5, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0,
    4, 0, 0, 2, 0, 0, 0, 4, 5, 0, 0, 2, 5, 0, 0, 0, 3, 0, 0, 3,
    0, 0, 0, 4, 0, 0, 0, 4, 0, 3, 0, 0, 2, 0, 0, 0, 0, 0, 2, 1,
    0, 0, 6, 3, 0, 3, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 6,
    0, 0, 0, 5, 2, 3, 3, 0, 0, 0, 0, 5, 0, 0, 5, 0, 0, 0, 3, 4,
    2, 0, 0, 3, 0, 1, 3, 0, 0, 1, 1, 0, 0, 0, 0, 4, 0, 0, 3, 0,
    5, 0, 3, 3, 0, 0, 0, 0, 1, 6, 0, 0, 0, 5, 0, 0, 0, 4, 0, 2,
    0, 0, 4, 0, 2, 0, 0, 0, 3, 3, 0, 4, 5, 1, 0, 2, 0, 0, 3, 0,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 5, 2, 0, 4, 0, 0, 5, 0, 0, 1,
    0, 0, 0, 3, 3, 0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 0, 0, 5, 4, 0,
    2, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 3, 0,
    0, 5, 0, 1, 0, 4, 0, 4, 0, 0, 0, 2, 0, 0, 0, 4, 0, 2, 0, 5,
    0, 0, 5, 0, 3, 0, 0, 1, 0, 0, 4, 0, 1, 0, 2, 0, 0, 0, 0, 4,
    0, 3, 0, 0, 0, 4, 0, 4, 0, 4, 0, 0, 4, 0, 0, 0, 0, 3, 0, 4,
    0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 3, 2,
    0, 0, 1, 0, 0, 3, 0, 0, 5, 5, 0, 0, 4, 0, 5, 0, 0, 2, 3, 1,
    0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 5, 6, 2, 2, 0, 0,
    1, 0, 0, 3, 0, 0, 0, 5, 0, 0, 4, 0, 0, 2, 0, 0, 5, 0, 0, 3,
    0, 0, 4, 0, 0, 0, 3, 0, 0, 5, 0, 0, 1, 0, 0, 5, 0, 4, 0, 4,
    0, 0, 0, 4, 0, 0, 1, 0, 0, 5, 0, 0, 4, 0, 0, 1, 0, 1, 0, 4,
    0, 4, 0, 0, 0, 0, 4, 0, 0, 5, 0, 4, 0, 0, 4, 0, 0, 0, 0, 5,
    0, 0, 0, 2, 0, 0, 5, 0, 0, 3, 0, 0, 4, 0, 0, 4, 0, 4, 0, 0,
    3, 0, 0, 4, 1, 0, 0, 0, 2, 0, 0, 5, 0, 0, 0, 2, 0, 0, 4, 0,
    0, 0, 3, 0, 5, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 5, 0, 0, 0, 5,
    0, 0, 4, 0, 1, 0, 0, 0, 4, 0, 4, 0, 0, 2, 0, 0, 3, 0, 3, 0,
    3, 0, 0, 0, 4, 0, 0, 3, 0, 5, 0, 5, 0, 1, 0, 0, 0, 4, 0, 3,
    0, 0, 4, 4, 4, 0, 0, 3, 0, 0, 0, 4, 0, 0, 4, 0, 3, 0, 4, 0,
    0, 0, 3, 0, 0, 0, 5, 0, 4, 4, 5, 0, 0, 4, 0, 4, 0, 0, 0, 5,
    0, 0, 3, 0, 0, 3, 0, 0, 1, 0, 0, 5, 0, 0, 0, 5, 5, 0, 0, 0,
    3, 5, 0, 0, 0, 5, 3, 0, 0, 0, 4, 0, 2, 5, 0, 0, 0, 4, 0, 0,
    0, 4, 0, 0, 0, 4, 0, 0, 1, 5, 0, 0, 0, 0, 5, 0, 0, 2, 0, 0,
    0, 5, 0, 4, 0, 2, 0, 4, 0, 0, 0, 4, 0, 3, 0, 0, 0, 4, 0, 0,
    3, 0, 3, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 2,
    0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 1,
    1, 0, 3, 6, 0, 3, 0, 0, 1, 0, 0, 4, 0, 0, 4, 0, 1, 6, 0, 1,
    0, 0, 0, 4, 0, 2, 0, 5, 0, 5, 0, 4, 0, 4, 0, 0, 5, 0, 3, 0,
    4, 0, 1, 0, 0, 0, 5, 0, 2, 0, 3, 0, 0, 5, 0, 0, 3, 0, 4, 0,
    4, 0, 3, 0, 4, 0, 4, 0, 3, 0, 4, 0, 5, 0, 3, 0, 5, 0, 1, 0,
    0, 4, 0, 3, 0, 3, 0, 0, 0, 4, 0, 4, 0, 3, 0, 0, 0, 3, 0, 0,
    1, 0, 0, 1, 0, 2, 0, 0, 3, 0, 0, 4, 0, 0, 5, 2, 0, 0, 5, 0,
    0, 0, 0, 1, 0, 0, 1, 0, 0, 5, 0, 0, 0, 2, 0, 0, 5, 5, 0, 0,
    3, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0,
    4, 0, 0, 5, 4, 0, 0, 0, 4, 1, 5, 0, 0, 3, 0, 0, 0, 3, 0, 0,
    0, 3, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 4, 0, 0, 1, 0,
    0, 4, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 5, 0, 1, 0, 3, 0, 0, 5,
    0, 4, 0, 5, 0, 0, 5, 0, 3, 0, 0, 4, 4, 0, 0, 3, 0, 3, 0, 1,
    4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0,
    0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 4, 0,
    2, 0, 1, 0, 0, 0, 5, 0, 0, 4, 0, 2, 0, 3, 0, 0, 0, 4, 0, 0,
    5, 0, 0, 0, 1, 0, 0, 1, 0, 0, 3, 0, 0, 0, 5, 2, 0, 1, 0, 5,
    0, 1, 0, 4, 0, 4, 0, 0, 0, 0, 3, 0, 1, 0, 5, 0, 0, 0, 0, 0,
    4, 0, 4, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 5, 0, 1, 0, 1, 0, 0,
    5, 0, 0, 0, 4, 0, 4, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0,
    4, 0, 4, 0, 4, 0, 1, 0, 3, 0, 0, 0, 0, 0, 0, 4, 0, 0, 4, 0,
    0, 3, 0, 0, 3, 4, 0, 0, 3, 0, 0, 4, 0, 0, 3, 0, 0, 0, 3, 0,
    0, 2, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 5, 0, 0, 0, 0, 5, 0, 0,
    4, 0, 1, 0, 0, 0, 0, 4, 0, 3, 0, 0, 5, 0, 5, 0, 3, 0, 3, 0,
    0, 0, 4, 0, 3, 0, 1, 0, 0, 4, 5, 0, 0, 3, 0, 0, 1, 0, 0, 2,
    2, 1, 5, 0, 0, 0, 3, 0, 3, 5, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0,
    0, 3, 0, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 4, 3,
    0, 0, 0, 4, 0, 1, 5, 0, 3, 0, 0, 5, 0, 0, 5, 5, 0, 3, 0, 0,
    0, 3, 0, 0, 0, 4, 0, 0, 5, 5, 4, 5, 0, 0, 0, 0, 4, 5, 0, 0,
    0, 4, 0, 0, 0, 6, 1, 0, 0, 0, 2, 1, 0, 0, 0, 0, 5, 0, 4, 4,
    0, 4, 0, 0, 0, 2, 0, 4, 0, 4, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1,
    3, 4, 5, 0, 0, 0, 4, 1, 0, 0, 2, 0, 0, 0, 0, 5, 0, 4, 0, 0,
    0, 0, 4, 4, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 4, 0, 1, 0, 0,
    0, 4, 0, 1, 4, 5, 4, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 5, 0,
    0, 2, 3, 3, 4, 4, 0, 0, 4, 4, 0, 5, 0, 0, 2, 0, 0, 0, 3, 5,
    0, 0, 1, 5, 5, 0, 0, 0, 1, 1, 4, 0, 0, 3, 0, 0, 1, 0, 0, 1,
    0, 6, 5, 0, 0, 4, 0, 0, 0, 5, 4, 0, 0, 0, 4, 0, 5, 0, 0, 0,
    5, 0, 5, 4, 4, 0, 0, 0, 3, 3, 0, 0, 0, 3, 4, 1, 0, 4, 0, 0,
    0, 4, 4, 0, 1, 4, 0, 0, 5, 0, 0, 0, 5, 5, 0, 5, 5, 0, 0, 0,
    3, 0, 0, 0, 4, 5, 4, 3, 2, 3, 0, 2, 2, 0, 0, 1, 5, 0, 0, 1,
    0, 0, 3, 3, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 1, 0, 0,
    5, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 5, 4, 0, 0,
    1, 0, 5, 3, 0, 0, 0, 2, 5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 5,
    0, 0, 5, 4, 0, 0, 0, 4, 4, 2, 1, 0, 2, 5, 0, 3, 1, 5, 0, 0,
    0, 0, 5, 0, 0, 2, 0, 0, 0, 5, 0, 0, 5, 1, 3, 5, 1, 5, 0, 0,
    2, 4, 0, 3, 0, 0, 5, 0, 0, 5, 0, 0, 0, 1, 0, 1, 2, 0, 3, 0,
    2, 0, 0, 0, 0, 0, 5, 3, 0, 0, 0, 0, 1, 2, 1, 0, 3, 0, 0, 0,
    0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 5, 0, 0, 0, 0,
    5, 4, 0, 1, 4, 1, 0, 0, 4, 0, 0, 0, 0, 3, 3, 2, 0, 1, 1, 0,
    0, 4, 0, 0, 0, 3, 2, 0, 0, 3, 0, 0, 2, 5, 3, 3, 4, 0, 0, 0,
    0, 4, 0, 0, 0, 4, 0, 4, 5, 0, 5, 0, 5, 0, 0, 0, 5, 0, 0, 0,
    5, 4, 0, 0, 5, 2, 3, 2, 1, 2, 5, 0, 0, 0, 4, 0, 0, 0, 3, 0,
    0, 0, 0, 4, 2, 5, 2, 0, 2, 3, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    5, 0, 0, 0, 2, 1, 0, 0, 0, 0, 4, 0, 0, 5, 4, 0, 4, 0, 0, 0,
    0, 0, 0, 5, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 3, 0, 0,
    0, 0, 0, 0, 3, 4, 0, 0, 0, 0, 0, 4, 0, 3, 0, 0, 3, 0, 1, 2,
    0, 1, 0, 2, 1, 3, 0, 0, 1, 0, 0, 0, 0, 3, 0, 2, 5, 0, 3, 0,
    5, 0, 4, 0, 5, 0, 0, 3, 0, 1, 0, 2, 3, 4, 0, 4, 0, 0, 0, 4,
    0, 5, 0, 0, 0, 5, 3, 0, 0, 5, 0, 4, 0, 0, 4, 0, 4, 0, 0, 0,
    0, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 5, 0, 0, 5, 2, 0, 2, 0, 2,
    0, 0, 0, 0, 4, 1, 2, 5, 0, 2, 1, 0, 0, 5, 0, 4, 0, 4, 0, 0,
    0, 0, 3, 5, 0, 0, 0, 1, 0, 0, 5, 0, 4, 0, 0, 0, 0, 0, 5, 0,
    5, 0, 0, 0, 2, 0, 0, 0, 0, 5, 0, 3, 0, 0, 2, 0, 4, 0, 5, 0,
    1, 5, 0, 4, 0, 3, 0, 0, 3, 5, 0, 1, 5, 0, 0, 0, 0, 3, 5, 0,
    4, 1, 0, 0, 0, 1, 3, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0, 1, 1, 5,
    0, 4, 0, 0, 0, 0, 0, 5, 0, 4, 4, 0, 0, 5, 0, 0, 0, 1, 0, 4,
    1, 4, 0, 3, 0, 2, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 4, 3, 0,
    0, 2, 3, 0, 0, 5, 0, 0, 0, 0, 3, 0, 4, 0, 0, 2, 0, 0, 0, 3,
    0, 0, 2, 0, 0, 2, 0, 0, 1, 6, 0, 0, 2, 4, 0, 0, 2, 0, 0, 0,
    0, 0, 6, 4, 0, 4, 4, 3, 1, 0, 0, 0, 5, 0, 0, 5, 1, 0, 0, 0,
    3, 2, 1, 3, 2, 0, 0, 5, 5, 0, 0, 1, 1, 5, 4, 0, 0, 5, 4, 0,
    0, 4, 0, 0, 3, 0, 0, 0, 5, 0, 0, 2, 0, 0, 5, 0, 0, 0, 5, 0,
    0, 0, 3, 4, 0, 0, 0, 0, 1, 5, 0, 0, 0, 4, 0, 0, 5, 0, 1, 1,
    0, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 0, 3, 0, 6, 0, 6, 0, 0, 5,
    0, 0, 5, 0, 0, 0, 4, 4, 4, 0, 0, 3, 5, 0, 0, 2, 0, 0, 0, 4,
    0, 0, 5, 4, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 2, 0, 0, 0,
    4, 2, 1, 4, 0, 4, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 3, 1, 0,
    1, 4, 1, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 4, 0, 0, 0, 0,
    4, 5, 2, 1, 0, 0, 5, 1, 0, 0, 0, 4, 0, 0, 5, 0, 0, 5, 3, 0,
    0, 0, 0, 2, 0, 0, 0, 0, 1, 6, 0, 0, 0, 0, 4, 5, 0, 0, 2, 0,
    2, 0, 0, 0, 3, 3, 3, 3, 4, 1, 0, 0, 0, 4, 1, 0, 0, 4, 0, 0,
    5, 0, 0, 0, 3, 5, 0, 0, 3, 3, 0, 0, 1, 1, 0, 0, 0, 2, 0, 0,
    0, 5, 0, 0, 0, 3, 0, 0, 0, 0, 5, 5, 0, 0, 0, 5, 4, 4, 0, 3,
    0, 3, 0, 0, 2, 0, 0, 0, 0, 5, 3, 3, 4, 2, 1, 4, 1, 2, 0, 0,
    0, 4, 1, 0, 0, 0, 0, 4, 0, 0, 2, 4, 0, 0, 3, 0, 0, 0, 5, 0,
    0, 0, 3, 3, 0, 0, 5, 0, 0, 5, 4, 5, 4, 0, 0, 5, 5, 0, 0, 0,
    3, 0, 0, 1, 5, 0, 0, 1, 0, 0, 0, 1, 2, 4, 0, 0, 0, 0, 5, 0,
    0, 0, 5, 2, 0, 0, 0, 4, 1, 0, 0, 0, 3, 1, 0, 0, 4, 2, 3, 4,
    0, 0, 4, 2, 0, 2, 4, 0, 4, 0, 4, 0, 1, 5, 3, 0, 0, 0, 2, 0,
    0, 5, 2, 5, 0, 0, 0, 0, 5, 1, 4, 0, 0, 0, 5, 2, 1, 0, 2, 3,
    3, 0, 0, 2, 3, 1, 1, 0, 0, 5, 0, 0, 0, 0, 5, 5, 0, 0, 3, 3,
    0, 0, 0, 4, 0, 0, 5, 0, 0, 0, 4, 5, 0, 0, 3, 0, 0, 4, 1, 0,
    0, 0, 4, 1, 0, 0, 3, 0, 0, 3, 2, 0, 5, 0, 0, 0, 0, 4, 3, 3,
    3, 0, 5, 0, 1, 0, 2, 0, 5, 4, 1, 0, 0, 0, 4, 2, 1, 2, 1, 0,
    0, 3, 0, 0, 5, 4, 2, 0, 0, 2, 0, 0, 5, 0, 0, 0, 0, 2, 0, 0,
    2, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 4, 0, 0, 3, 4, 4, 0, 0, 2,
    0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 3, 5, 4, 0, 0, 3,
    4, 1, 1, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 0, 0, 4, 0, 0, 0,
    0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 1, 0, 0, 0, 0, 4,
    0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0,
    0, 5, 0, 5, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 4, 0,
    0, 0, 0, 5, 5, 0, 0, 4, 0, 0, 0, 5, 5, 4, 2, 0, 0, 0, 4, 0,
    0, 0, 0, 4, 0, 0, 0, 3, 3, 0, 0, 1, 0, 1, 0, 0, 0, 5, 0, 0,
    0, 0, 5, 4, 0, 0, 0, 5, 0, 0, 0, 1, 2, 3, 0, 0, 0, 1, 5, 3,
    0, 0, 0, 1, 5, 0, 1, 0, 1, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 3,
    0, 0, 0, 5, 0, 0, 0, 0, 4, 4, 1, 2, 0, 4, 0, 0, 0, 0, 3, 0,
    0, 0, 4, 0, 4, 4, 3, 2, 0, 4, 0, 5, 0, 0, 0, 0, 0, 1, 0, 1,
    0, 0, 0, 4, 1, 0, 0, 1, 0, 0, 3, 0, 0, 4, 0, 0, 0, 2, 4, 4,
    0, 0, 4, 5, 0, 3, 3, 0, 0, 4, 2, 3, 3, 5, 0, 0, 0, 3, 0, 0,
    0, 0, 3, 0, 0, 0, 5, 0, 0, 5, 3, 4, 0, 0, 4, 4, 1, 0, 1, 0,
    0, 0, 0, 4, 4, 1, 4, 2, 0, 0, 2, 5, 0, 0, 0, 0, 3, 0, 0, 5,
    4, 0, 0, 4, 4, 0, 0, 0, 4, 0, 0, 3, 0, 0, 0, 1, 0, 0, 5, 0,
    0, 0, 4, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 3, 0,
    1, 0, 0, 0, 4, 0, 0, 4, 0, 4, 0, 3, 0, 4, 0, 1, 4, 0, 4, 5,
    0, 3, 0, 3, 0, 0, 0, 4, 0, 0, 4, 0, 0, 0, 0, 3, 0, 3, 0, 3,
    0, 4, 0, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 0, 0, 4, 0, 1, 0, 0,
    3, 0, 0, 3, 0, 0, 5, 0, 5, 0, 4, 0, 5, 0, 3, 0, 0, 4, 0, 4,
    0, 0, 0, 1, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 3,
    0, 3, 0, 0, 0, 3, 0, 0, 5, 2, 0, 2, 0, 0, 0, 5, 0, 0, 4, 0,
    1, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 5,
    4, 0, 1, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0, 4, 5, 0, 0, 3, 0, 0,
    4, 0, 1, 0, 0, 4, 0, 0, 2, 0, 5, 0, 0, 3, 0, 0, 5, 0, 0, 4,
    0, 0, 4, 0, 4, 0, 0, 4, 4, 0, 3, 0, 1, 4, 4, 0, 0, 0, 5, 0,
    0, 1, 0, 0, 0, 0, 0, 3, 4, 0, 3, 0, 0, 5, 0, 0, 5, 0, 0, 5,
    0, 0, 4, 0, 0, 4, 0, 1, 0, 4, 3, 0, 0, 0, 0, 5, 0, 0, 2, 0,
    0, 0, 0, 4, 0, 2, 0, 0, 4, 0, 1, 0, 0, 4, 0, 2, 0, 0, 5, 0,
    0, 0, 3, 0, 0, 3, 4, 0, 0, 4, 0, 0, 0, 3, 0, 0, 4, 0, 0, 4,
    0, 0, 0, 4, 0, 0, 5, 0, 0, 5, 0, 4, 0, 5, 0, 1, 0, 1, 0, 0,
    0, 0, 5, 0, 5, 0, 0, 3, 0, 3, 0, 0, 3, 0, 0, 0, 0, 0, 0, 5,
    0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 1, 4, 0, 4, 0, 2, 1, 0, 4, 0,
    0, 4, 0, 0, 0, 4, 0, 0, 1, 0, 0, 0, 5, 0, 0, 4, 0, 0, 4, 0,
    0, 0, 4, 0, 3, 0, 0, 0, 4, 0, 0, 1, 0, 3, 0, 1, 0, 0, 3, 0,
    0, 2, 0, 0, 0, 5, 0, 0, 4, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 4,
    0, 0, 0, 5, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 4, 0, 0, 4, 0, 3,
    2, 0, 5, 0, 5, 0, 0, 4, 0, 0, 0, 2, 0, 0, 3, 0, 0, 0, 5, 0,
    0, 2, 0, 5, 0, 3, 0, 0, 4, 0, 0, 1, 0, 0, 5, 0, 0, 5, 0, 0,
    1, 0, 2, 0, 0, 0, 0, 4, 0, 0, 0, 4, 0, 3, 4, 4, 4, 0, 0, 0,
    0, 4, 4, 1, 2, 0, 0, 0, 5, 0, 3, 0, 0, 3, 0, 0, 0, 0, 0, 5,
    0, 4, 5, 5, 0, 4, 1, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 5,
    0, 4, 0, 0, 0, 4, 0, 3, 0, 0, 4, 0, 1, 2, 0, 0, 0, 3, 0, 0,
    4, 1, 5, 2, 1, 4, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0,
    4, 0, 0, 6, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 5, 0, 0,
    5, 0, 0, 5, 0, 0, 0, 5, 3, 0, 1, 0, 0, 0, 0, 0, 3, 4, 4, 0,
    0, 0, 3, 0, 3, 0, 0, 0, 3, 0, 0, 4, 0, 4, 0, 0, 0, 3, 0, 5,
    0, 4, 5, 0, 1, 0, 1, 0, 4, 1, 0, 3, 0, 0, 0, 5, 0, 4, 0, 0,
    0, 3, 0, 0, 0, 0, 0, 4, 3, 0, 0, 0, 3, 0, 4, 2, 4, 0, 0, 0,
    4, 0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 3, 0, 0, 4, 0, 0, 5, 5, 5,
    5, 4, 3, 5, 5, 0, 3, 0, 0, 5, 0, 0, 0, 4, 2, 5, 0, 3, 3, 0,
    1, 4, 0, 4, 0, 2, 0, 0, 0, 5, 4, 0, 0, 0, 3, 0, 3, 0, 4, 0,
    0, 1, 0, 0, 4, 0, 0, 5, 0, 0, 0, 3, 0, 0, 3, 0, 0, 3, 4, 0,
    0, 0, 3, 4, 3, 0, 0, 1, 0, 0, 3, 3, 5, 0, 0, 0, 4, 0, 0, 0,
    0, 0, 1, 0, 1, 0, 0, 5, 0, 1, 3, 0, 0, 4, 0, 5, 5, 3, 0, 0,
    0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 4, 0, 0, 4, 0, 0, 4, 4, 4, 0,
    4, 0, 4, 0, 5, 2, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 5,
    0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 3, 6, 2,
    0, 0, 1, 0, 0, 0, 0, 1, 4, 0, 0, 5, 0, 1, 0, 3, 0, 0, 0, 5,
    0, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 1, 0, 0, 0, 0, 3,
    0, 0, 0, 5, 0, 0, 0, 4, 0, 1, 0, 0, 0, 0, 3, 0, 3, 0, 0, 0,
    4, 0, 0, 2, 0, 0, 0, 0, 5, 0, 0, 0, 2, 0, 0, 0, 0, 5, 0, 0,
    0, 4, 0, 0, 0, 4, 0, 0, 0, 4, 3, 0, 0, 0, 0, 3, 0, 0, 0, 5,
    0, 4, 0, 0, 4, 0, 0, 3, 0, 4, 1, 0, 2, 0, 0, 4, 1, 0, 0, 0,
    1, 0, 0, 5, 0, 5, 0, 0, 0, 4, 0, 0, 0, 0, 3, 0, 0, 0, 4, 0,
    0, 0, 0, 0, 4, 0, 0, 0, 0, 6, 0, 3, 0, 0, 4, 0, 0, 4, 0, 5,
    4, 4, 0, 0, 4, 0, 1, 0, 0, 0, 5, 0, 4, 0, 0, 0, 3, 0, 4, 0,
    0, 0, 5, 0, 3, 2, 0, 1, 0, 0, 4, 0, 2, 0, 0, 0, 4, 0, 0, 5,
    0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 5, 0, 0, 0, 4, 0, 3, 0, 0, 5,
    0, 0, 5, 0, 0, 5, 0, 4, 0, 0, 5, 0, 3, 0, 4, 0, 3, 0, 0, 0,
    0, 4, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 1, 0, 0, 0, 0, 0,
    3, 0, 1, 2, 0, 3, 0, 1, 0, 0, 3, 0, 0, 4, 0, 5, 3, 0, 0, 4,
    0, 5, 0, 1, 0, 1, 0, 2, 0, 0, 5, 0, 3, 0, 0, 0, 4, 0, 5, 0,
    0, 3, 0, 0, 0, 0, 4, 0, 0, 0, 0, 4, 0, 1, 0, 5, 0, 1, 0, 4,
    0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 1, 0, 0, 4, 0, 5, 4, 3, 0, 1,
    0, 3, 0, 0, 0, 0, 5, 0, 3, 0, 5, 0, 0, 0, 0, 0, 5, 0, 0, 0,
    4, 0, 0, 5, 5, 0, 0, 0, 0, 3, 0, 0, 3, 0, 0, 5, 0, 0, 5, 0,
    0, 5, 5, 0, 1, 4, 0, 0, 5, 0, 4, 5, 0, 0, 0, 4, 0, 0, 5, 0,
    4, 0, 4, 0, 4, 0, 0, 0, 0, 5, 0, 0, 3, 0, 3, 0, 4, 0, 0, 2,
    0, 5, 0, 0, 0, 5, 0, 0, 5, 0, 3, 0, 0, 4, 0, 0, 4, 0, 3, 2,
    0, 0, 3, 0, 0, 3, 3, 0, 4, 0, 0, 0, 4, 0, 0, 1, 0, 0, 3, 0,
    1, 0, 0, 0, 0, 4, 0, 3, 0, 0, 3, 0, 1, 0, 0, 1, 0, 5, 2, 0,
    0, 0, 2, 4, 2, 0, 0, 4, 0, 0, 4, 0, 1, 0, 0, 3, 0, 0, 0, 4,
    2, 1, 0, 3, 0, 4, 0, 4, 5, 4, 1, 0, 0, 4, 0, 0, 5, 0, 3, 1,
    0, 0, 0, 4, 4, 1, 2, 0, 2, 3, 0, 4,
};

static const uint16_t g_en_us_bucket_starts[] = {
    0, 244, 577, 717, 921, 1115, 1558, 1668, 1814, 1953, 2285, 2298, 2349, 2590,
    2783, 3035, 3331, 3551, 3562, 3901, 4195, 4471, 4662, 4756, 4812, 4847, 4914, 4938,
    4938,
};

static size_t hyphenation_pattern_bucket(unsigned char ch) {
    if (ch == '.') return 0;
    if (ch >= 'a' && ch <= 'z') return (size_t)(ch - 'a' + 1);
    return 27;
}

static unsigned char hyphenation_padded_char(const char* word, size_t length,
                                             size_t position) {
    if (position == 0 || position == length + 1) return '.';
    unsigned char ch = (unsigned char)word[position - 1];
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

bool layout_hyphenation_en_us_language(const char* lang) {
    if (!lang || (lang[0] != 'e' && lang[0] != 'E') ||
        (lang[1] != 'n' && lang[1] != 'N')) return false;
    if (lang[2] == '\0') return true;
    return (lang[2] == '-') &&
        (lang[3] == 'u' || lang[3] == 'U') &&
        (lang[4] == 's' || lang[4] == 'S') && lang[5] == '\0';
}

size_t layout_hyphenation_en_us_next_break(const char* word, size_t length,
                                           size_t after) {
    if (!word || length < 5 || length > SIZE_MAX - 2) return SIZE_MAX;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)word[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) {
            return SIZE_MAX;
        }
    }

    size_t first = after < length ? after + 1 : length;
    if (first < 2) first = 2;
    size_t last = length - 2;
    for (size_t candidate = first; candidate <= last; candidate++) {
        uint8_t highest_level = 0;
        size_t padded_length = length + 2;
        for (size_t start = 0; start < padded_length; start++) {
            unsigned char first_char = hyphenation_padded_char(word, length, start);
            size_t bucket = hyphenation_pattern_bucket(first_char);
            size_t begin = g_en_us_bucket_starts[bucket];
            size_t end = g_en_us_bucket_starts[bucket + 1];
            for (size_t pattern_index = begin; pattern_index < end; pattern_index++) {
                size_t pattern_length = g_en_us_pattern_lengths[pattern_index];
                if (start + pattern_length > padded_length) continue;
                const char* pattern = g_en_us_pattern_data +
                    g_en_us_pattern_offsets[pattern_index];
                bool matches = true;
                for (size_t offset = 0; offset < pattern_length; offset++) {
                    if ((unsigned char)pattern[offset] !=
                        hyphenation_padded_char(word, length, start + offset)) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;
                size_t level_position = candidate + 1;
                if (level_position < start) continue;
                size_t local_level = level_position - start;
                size_t level_length = g_en_us_level_lengths[pattern_index];
                if (local_level < level_length) {
                    uint8_t level = g_en_us_level_data[
                        g_en_us_level_offsets[pattern_index] + local_level];
                    if (level > highest_level) highest_level = level;
                }
            }
        }
        if (highest_level & 1) return candidate;
    }
    return SIZE_MAX;
}
