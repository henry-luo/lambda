package richards;

/** Reuse the upstream scheduler with JetStream's idle count and counters. */
public final class JetRichards extends Scheduler {
    private int queued,held;
    @Override void createIdler(int id,int priority,Packet work,TaskState state) {
        IdleTaskDataRecord data=new IdleTaskDataRecord();data.setCount(1000);
        createTask(id,priority,work,state,(packet,word)->{
            IdleTaskDataRecord d=(IdleTaskDataRecord)word;d.setCount(d.getCount()-1);
            if(d.getCount()==0)return holdSelf();
            if((d.getControl()&1)==0){d.setControl(d.getControl()/2);return release(DEVICE_A);}
            d.setControl((d.getControl()/2)^53256);return release(DEVICE_B);
        },data);
    }
    @Override TaskControlBlock queuePacket(Packet p){queued++;return super.queuePacket(p);}
    @Override TaskControlBlock holdSelf(){held++;return super.holdSelf();}
    public boolean run(){super.start();return queued==2322&&held==928;}
}
