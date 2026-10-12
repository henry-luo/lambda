pub fn model(id) => <cached_counter id:id>
view <cached_counter> state count:0 { <button id:~.id,string(count)> }
on click(evt) { count=count+1 }
