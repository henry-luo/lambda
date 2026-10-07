import java.util.ArrayList;
import java.util.Arrays;
import static java.util.Arrays.copyOf;

/** Native jq bytecode loop, forkable stack and host-managed call frames. */
final class JqVM {
    record Label(long id){}
    record Closure(int fn,Frame env){}
    static final class Frame {
        final Frame env,caller;final int retpc,fn,forkBase;final Object[] locals;final Closure[] params;
        Frame(JqCompiler.Program p,int fn,Frame env,Frame caller,int retpc,int forks){this.fn=fn;this.env=env;this.caller=caller;this.retpc=retpc;forkBase=forks;JqCompiler.Fn f=p.functions()[fn];locals=new Object[f.nlocals];params=new Closure[f.nparams];}
    }
    static final class Fork {int kind,pc,top,limit,subexp,index;Frame frame;Object path,vat,aux,aux2,aux3;boolean active=true;long label;}
    Object[] stack=new Object[4096];int[] previous=new int[4096];int top=-1,limit=-1;Fork[] forks=new Fork[1024];int nforks,subexp;long labels;Frame frame;Object path,vat,error;
    void push(Object value){int index=Math.max(top,limit)+1;if(index>=stack.length){stack=copyOf(stack,stack.length*2);previous=copyOf(previous,previous.length*2);}stack[index]=value;previous[index]=top;top=index;}
    Object pop(){Object v=stack[top];int at=top;top=previous[top];if(at>limit)stack[at]=null;return v;}
    Fork fork(int kind,int pc){if(nforks==forks.length)forks=copyOf(forks,forks.length*2);Fork f=new Fork();f.kind=kind;f.pc=pc;f.top=top;f.limit=limit;f.frame=frame;f.path=path;f.vat=vat;f.subexp=subexp;forks[nforks++]=f;limit=Math.max(top,limit);return f;}
    void restore(Fork f){top=f.top;limit=f.limit;frame=f.frame;path=f.path;vat=f.vat;subexp=f.subexp;}
    void removeFork(){forks[--nforks]=null;}
    Frame hop(Frame f,int count){for(int i=0;i<count;i++)f=f.env;return f;}
    boolean pathCheck(Object t){return path==null||subexp!=0||t==vat||JqValues.type(t)==JqValues.type(vat)&&JqValues.compare(t,vat)==0;}
    void pathAppend(Object key,Object value){if(path!=null&&subexp==0){ArrayList<Object> out=new ArrayList<>(JqValues.arr(path));out.add(key);path=out;vat=value;}}
    void each(Object container,Object keys,int index){Object key=container instanceof java.util.List?index:JqValues.arr(keys).get(index),value=JqValues.index(container,key);pathAppend(key,value);push(value);}
    static boolean more(double from,double upto,double step){return step>0?from<upto:step<0&&from>upto;}
    static ArrayList<Object> run(JqCompiler.Program program,Object input){return new JqVM().execute(program,input);}
    ArrayList<Object> execute(JqCompiler.Program p,Object input){int[] code=p.code();frame=new Frame(p,0,null,null,-1,0);push(input);int pc=p.functions()[0].entry,mode=0;ArrayList<Object> outputs=new ArrayList<>();
        while(true){if(mode==1){if(nforks==0)return outputs;Fork f=forks[nforks-1];
                if(f.kind==1){restore(f);pc=f.pc;removeFork();mode=0;continue;}
                if(f.kind==4){restore(f);int index=f.index++;pc=f.pc;if(index+1>=JqValues.size(f.aux))removeFork();else limit=Math.max(top,limit);each(f.aux,f.aux2,index);mode=0;continue;}
                if(f.kind==5){restore(f);double cur=JqValues.num(f.aux),step=JqValues.num(f.aux3),next=cur+step;pc=f.pc;if(more(next,JqValues.num(f.aux2),step)){f.aux=next;limit=Math.max(top,limit);}else removeFork();push(cur);mode=0;continue;}
                if(f.kind==3)forks[f.index].active=true;removeFork();continue;}
            if(mode==2){if(nforks==0)throw JqValues.error("uncaught jq error: "+(error instanceof Label?error:JqValues.json(error)));Fork f=forks[nforks-1];removeFork();if(f.kind!=2||!f.active)continue;
                if(f.label!=0){if(error instanceof Label l&&l.id==f.label){restore(f);mode=1;}continue;}// Catch replaces the saved input; retaining it corrupts surrounding subexpressions.
                if(error instanceof Label)continue;restore(f);pop();push(error);pc=f.pc;mode=0;continue;}
            int op=code[pc++];try{switch(op){
                case 1:push(stack[top]);break;
                case 2:pop();break;
                case 3:pop();push(p.constants()[code[pc++]]);break;
                case 4:pop();push(new ArrayList<>());break;
                case 5:{Object v=pop();push(new java.util.LinkedHashMap<String,Object>());push(v);break;}
                case 6:push(stack[top]);subexp++;break;
                case 7:{Object a=pop(),b=pop();push(a);push(b);subexp--;break;}
                case 8:case 9:{Object t=pop(),k=op==9?p.constants()[code[pc++]]:pop();if(!pathCheck(t))throw JqValues.error("Invalid path expression");Object value=JqValues.index(t,k);pathAppend(k,value);push(value);break;}
                case 10:{Object c=pop();int type=JqValues.type(c);if(type!=5&&type!=6)throw JqValues.error("Cannot iterate over "+JqValues.typeName(c));if(!pathCheck(c))throw JqValues.error("Invalid path expression");if(JqValues.size(c)==0){mode=1;break;}Object keys=type==6?JqValues.keys(c,false):null;
                    if(JqValues.size(c)>1){Fork f=fork(4,pc);f.aux=c;f.aux2=keys;f.index=1;}each(c,keys,0);break;}
                case 11:{Object t=pop(),upper=pop(),lower=pop();if(path!=null&&subexp==0)throw JqValues.error("jq-core does not support slice paths");push(JqValues.slice(t,lower,upper));break;}
                case 12:fork(1,code[pc++]);break;
                case 13:pc=code[pc];break;
                case 14:{Object condition=pop();pc=JqValues.truth(condition)?pc+1:code[pc];break;}
                case 15:{Object inp=pop(),condition=pop();push(inp);pc=JqValues.truth(condition)?pc+1:code[pc];break;}
                case 16:pc=JqValues.truth(stack[top])?pc+1:code[pc];break;
                case 17:mode=1;break;
                case 18:case 19:case 20:case 21:case 22:case 35:case 36:{Frame f=hop(frame,code[pc++]);int slot=code[pc++];
                    if(op==18)f.locals[slot]=pop();else if(op==19){Object inp=pop();f.locals[slot]=pop();push(inp);}
                    else if(op==20||op==21){pop();push(f.locals[slot]);if(op==21)f.locals[slot]=null;}
                    else if(op==22)JqValues.arr(f.locals[slot]).add(pop());
                    else if(op==35){f.locals[slot]=new Label(++labels);fork(2,-1).label=labels;}
                    else{error=f.locals[slot];mode=2;}break;}
                case 23:{Object inp=pop(),value=pop(),key=pop(),object=pop();if(!(key instanceof String s))throw JqValues.error("Object keys must be strings");push(JqValues.with(object,s,value));push(inp);break;}
                case 24:{int nargs=code[pc++];pop();double step=nargs==3?JqValues.num(pop()):1,upto=JqValues.num(pop()),from=JqValues.num(pop());if(!more(from,upto,step)){mode=1;break;}if(more(from+step,upto,step)){Fork f=fork(5,pc);f.aux=from+step;f.aux2=upto;f.aux3=step;}push(from);break;}
                case 25:{Object value=pop();push(path);push(vat);push(subexp);path=new ArrayList<>();vat=value;subexp=0;push(value);break;}
                case 26:{Object r=pop();if(!pathCheck(r))throw JqValues.error("Invalid path expression");Object result=path;subexp=((Number)pop()).intValue();vat=pop();path=pop();push(result);break;}
                case 27:{int id=code[pc++],nargs=code[pc++];Object inp=pop();Object[] args=new Object[nargs];for(int i=nargs-1;i>=0;i--)args[i]=pop();if(id==27&&!pathCheck(inp))throw JqValues.error("Invalid path expression");Object value=JqValues.nativeCall(id,inp,args);
                    if(id==27&&path!=null&&subexp==0){ArrayList<Object> out=new ArrayList<>(JqValues.arr(path));out.addAll(JqValues.arr(args[0]));path=out;vat=value;}push(value);break;}
                case 28:case 29:{int fn=code[pc++],hops=code[pc++],nargs=code[pc++];Frame site=frame,caller=frame;int retpc=pc+nargs;
                    if(op==29&&nforks==frame.forkBase&&frame.caller!=null){caller=frame.caller;retpc=frame.retpc;}Frame f=new Frame(p,fn,hop(frame,hops),caller,retpc,nforks);
                    for(int i=0;i<nargs;i++)f.params[i]=new Closure(code[pc+i],site);frame=f;pc=p.functions()[fn].entry;break;}
                case 30:case 31:{Frame owner=hop(frame,code[pc++]);Closure closure=owner.params[code[pc++]];Frame caller=frame;int retpc=pc;
                    if(op==31&&nforks==frame.forkBase&&frame.caller!=null){caller=frame.caller;retpc=frame.retpc;}frame=new Frame(p,closure.fn,closure.env,caller,retpc,nforks);pc=p.functions()[closure.fn].entry;break;}
                case 32:if(frame.caller==null){outputs.add(pop());mode=1;}else{pc=frame.retpc;frame=frame.caller;}break;
                case 33:fork(2,code[pc++]);break;
                case 34:{int at=nforks-1;while(at>=0&&!(forks[at].kind==2&&forks[at].active))at--;if(at>=0){forks[at].active=false;fork(3,-1).index=at;}break;}
                case 37:{int bop=code[pc++];pop();Object left=pop(),right=pop();push(JqValues.bin(bop,left,right));break;}
                case 38:push(0-JqValues.num(pop()));break;
                case 39:push(JqValues.truth(pop()));break;
                default:throw new IllegalArgumentException("invalid jq opcode "+op+" at "+(pc-1));
            }}catch(JqValues.Failure failure){error=failure.value;mode=2;}
        }
    }
}
