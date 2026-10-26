// D7.2.4v4/S12.1.1v3: import is inert; acquisition is explicit and queries are pure.
import c: .chinese.common
import syllables: .chinese.syllables
import pack: .chinese.pack
import query: .chinese.query
import reducer: .chinese.session
import learned: .chinese.user

pub let fuzzy_pairs = syllables.fuzzy_pairs
pub fn fuzzy_options() array => fuzzy_pairs

// prepared bytes also support deterministic hosts and fixtures without resource IO.
pub fn from_bytes(manifest,lexicon,abbrev,syllable_text,profile = "zh-Hans") map^ {
    let data = pack.from_bytes(manifest,lexicon,abbrev,profile)^;
    {*:data,syllables:syllables.inventory(syllable_text)}
}

pub pn load(profile = "zh-Hans",options = null) map^ {
    let opts = c.options(options,["directory"])^
    let home = sys.lambda.home#
    let root = home ++ "/package/ime/chinese/data/"
    let installed = [for (name in ["zh-Hans","zh-Hant"] where exists(root ++ name ++ "/manifest.json")) name]
    let valid = c.require(profile is string and (opts.directory != null or profile in installed),
        "unknown or unprepared profile: " ++ string(profile) ++ "; installed: " ++ join(installed,", "))^
    let directory = c.get(opts,"directory",root ++ profile)
    let path = c.require(directory is string,"directory must be a string")^
    let manifest = input(directory ++ "/manifest.json","json")^
    let lexicon = input(directory ++ "/lexicon.txt","binary")^
    let abbrev = input(directory ++ "/abbrev.txt","binary")^
    let inventory = input(home ++ "/package/ime/chinese/syllables.txt","text")^;
    return from_bytes(manifest,lexicon,abbrev,inventory,profile)^
}

pub fn candidates(engine,raw,options = null) map^ {
    query.candidates(engine,raw,options)^
}

pub fn session(engine,settings = null) map^ => reducer.create(engine,settings)^
pub fn key(engine,session,evt) map^ => reducer.key(engine,session,evt)^
pub fn configure(engine,session,settings) map^ => reducer.configure(engine,session,settings)^
pub fn settings(session) map^ => reducer.settings(session)^
pub fn with_user(session,user) map^ => reducer.with_user(session,user)^

pub pn load_user(path) map^ {
    let saved = input(path,"json")^
    let text = input(sys.lambda.home# ++ "/package/ime/chinese/syllables.txt","text")^;
    return learned.validate(saved,syllables.inventory(text))^
}

pub pn save_user(user,path) any^ {
    let text = input(sys.lambda.home# ++ "/package/ime/chinese/syllables.txt","text")^
    let checked = learned.validate(user,syllables.inventory(text))^;
    return output(learned.data(checked),path,{format:"json",atomic:true})^
}
