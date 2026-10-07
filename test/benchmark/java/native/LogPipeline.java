final class LogPipeline {
    static final String[] SERVICES={"api","worker","db","cache"},REGIONS={"us-east","eu-west","ap-south"};
    static final class Record {String timestamp,level="",service="",region="",route="",message="";int status,latency,bytes;}
    static final class Group {int count,errors,slow;long latency,bytes;}
    static String[] prepare(){String[] lines=new String[12000];for(int i=0;i<lines.length;i++){
        String time=String.format("2026-09-07T%02d:%02d:%02dZ",i%24,i%60,i*7%60),level=i%13==0?"ERROR":i%5==0?"WARN":"INFO",service=SERVICES[i%4];
        String prefix=i%3==0?time+" level="+level+" service="+service:time+" "+level+" "+service;
        lines[i]=prefix+" status="+(i%19==0?503:i%7==0?404:200)+" latency="+(i*37%900+4)+" region="+REGIONS[i*3%3]+" route="+(i%2==0?"/v1/items":"/v1/search")+" bytes="+(i*113%50000+512)+" message="+(i%11==0?"retry-scheduled":"request-complete");}return lines;}
    static Record parse(String line){String[] fields=line.split(" ",-1);Record r=new Record();r.timestamp=fields[0];int start=1;if(!fields[1].contains("=")){r.level=fields[1];r.service=fields[2];start=3;}
        for(int i=start;i<fields.length;i++){int sep=fields[i].indexOf('=');if(sep<0)continue;String k=fields[i].substring(0,sep),v=fields[i].substring(sep+1);switch(k){case "level"->r.level=v;case "service"->r.service=v;case "status"->r.status=Integer.parseInt(v);case "latency"->r.latency=Integer.parseInt(v);case "bytes"->r.bytes=Integer.parseInt(v);case "region"->r.region=v;case "route"->r.route=v;case "message"->r.message=v;default->throw new IllegalArgumentException(k);}}return r;}
    static long work(String[] lines){long checksum=0;for(int round=0;round<180;round++){java.util.HashMap<String,Group> groups=new java.util.HashMap<>();for(String s:SERVICES)groups.put(s,new Group());int accepted=0,rejected=0;
        for(String line:lines){Record r=parse(line);if(r.status>=500||r.level.equals("ERROR")){rejected++;continue;}Group g=groups.get(r.service);g.count++;g.latency+=r.latency;g.bytes+=r.bytes;if(r.latency>=500)g.slow++;accepted++;}
        checksum=(checksum+accepted*31L+rejected*17L+groups.get("api").latency+groups.get("worker").bytes+round)%1000000007;}return checksum;}
}
