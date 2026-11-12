import model: ~~.demo.sync.mod_model
import profile: ~~.demo.sync.mod_profile
let paths = profile.paths("/home/test")^
let initial = model.initial(paths.folder)^
let setup = model.update(initial,{type:"loaded"})^
let ticket = model.update(setup,{type:"pairing",user:"test",code:"aaaa-bbbb-cccc-dddd-eeee"})^
let paired = model.update(ticket,{type:"paired"})^
let running = model.update(paired,{type:"running",user:"test",folder:paths.folder})^
let status = model.update(running,{type:"status",status:{sessions:1,connected:false}})^
let failure = model.update(ticket,{type:"error",message:"expired"})^
let overlap = profile.paths("/home/test","/home/test/lambda-sync/private") ^ { ^ };
[paths.folder == "/home/test/lambda-sync",setup.mode == "setup",ticket.mode == "pairing",
    paired.pair_code == null,paired.input_code == "",running.mode == "active",running.user == "test",
    model.title(status)^ == "Watching for changes",failure.pair_code == null,failure.note == "expired",
    overlap is error,profile.directory(paths,"test")^ != profile.directory(paths,"other")^]
