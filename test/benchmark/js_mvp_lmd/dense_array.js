function work(){let a=[];for(let i=0;i<250000;i++)a[i]=i*0.5;let s=0;for(let p=0;p<50;p++)for(let i=0;i<a.length;i++)s+=a[i];return s} work()
