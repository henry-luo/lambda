package deltablue;
import som.Vector;

/** JetStream uses zero-based projection values and leaves the chain edit installed. */
public final class JetDeltaBlue {
    private static void check(boolean b){if(!b)throw new AssertionError("DeltaBlue plan");}
    private static void change(Planner p,Variable v,int value){
        EditConstraint edit=new EditConstraint(v,Strength.PREFERRED,p);
        Plan plan=p.extractPlanFromConstraints(Vector.with(edit));
        for(int i=0;i<10;i++){v.setValue(value);plan.execute();}edit.destroyConstraint(p);
    }
    public static boolean run(){
        Planner p=new Planner();Variable[] vars=new Variable[101];
        for(int i=0;i<vars.length;i++)vars[i]=new Variable();
        for(int i=0;i<100;i++)new EqualityConstraint(vars[i],vars[i+1],Strength.REQUIRED,p);
        new StayConstraint(vars[100],Strength.STRONG_DEFAULT,p);
        EditConstraint edit=new EditConstraint(vars[0],Strength.PREFERRED,p);
        Plan plan=p.extractPlanFromConstraints(Vector.with(edit));
        for(int i=0;i<100;i++){vars[0].setValue(i);plan.execute();check(vars[100].getValue()==i);}
        p=new Planner();Variable scale=Variable.value(10),offset=Variable.value(1000),src=null,dst=null;
        Variable[] dests=new Variable[100];
        for(int i=0;i<100;i++){src=Variable.value(i);dst=Variable.value(i);dests[i]=dst;
            new StayConstraint(src,Strength.DEFAULT,p);new ScaleConstraint(src,scale,offset,dst,Strength.REQUIRED,p);}
        change(p,src,17);check(dst.getValue()==1170);change(p,dst,1050);check(src.getValue()==5);
        change(p,scale,5);for(int i=0;i<99;i++)check(dests[i].getValue()==i*5+1000);
        change(p,offset,2000);for(int i=0;i<99;i++)check(dests[i].getValue()==i*5+2000);return true;
    }
}
