#!/usr/bin/gawk -f 
BEGIN{ kcnt=0;seq=0;lostcnt=0 }

# Parse only lines with filename:
/^[a-zA-Z_\./]*:/{
 if (seq==0){
  seq = $3-1;
 }
    seq++;
    if ( seq != $3 ){
        seq = $3;
#	lostcnt++;
        f="LOST"lostcnt;
        if (names[f]==""){
	    names[f]=f;
	    enu[kcnt]=f;
	    kcnt++;
	}
	count[f]++;
	lcount[lastlink,f]++;
	lastlink=f;
    }
    if (int($4)==0){
        f=$1$2;
#      f=$4$6;
        if (names[f]==""){
	    names[f]=$4;
	    infos[f]=" "$1$2
	    enu[kcnt]=f;
	    kcnt++;
	}
#       Write all posible Values:	
	if ((infos[f,$6]==0)&&(infos[f,"cnt"]<8)){
	    infos[f,$6]=1;
	    infos[f,"cnt"]++;
	    infos[f]=infos[f]"\\nVal="$6;
	    if (infos[f,"cnt"]==8){
	     infos[f]=infos[f]"\\n:";
	    }    
	}
	count[f]++;
	
	time=$5-lasttime;
	if (time < 0) { time += 4294967296}
	ltimes[lastlink,f]+=time;
	lcount[lastlink,f]++;
	lastlink=f;
	lasttime=$5;
    }
}
    
    

END{
    print "digraph ssh { ";
#    print "center=1";
#    print "nslimit=114.0";
#    print "mclimit=114.0";
    print "";

    print "node[ shape=record fontsize=10 ]"
    print "edge[ shape=record fontsize=10 ]"
	
    for(i=0;i<kcnt;i++){
	print "knote"i,"[label=\""names[enu[i]]"(#"count[enu[i]]")"infos[enu[i]] "\"]";
    }
    print "";
    for(i=0;i<kcnt;i++){
	for(j=0;j<kcnt;j++){
	    if (lcount[enu[i],enu[j]] != 0){
		zeit=0.5*ltimes[enu[i],enu[j]]/lcount[enu[i],enu[j]];
		if (zeit > 1000){
		    zeit=int(zeit/100)/10 "ms";
		}else{
		    zeit=zeit"us";
		}
		print "knote"i "->" "knote"j "[label=\"#"lcount[enu[i],enu[j]]"*"zeit   "\"]"
	    }
	}
    }
    print "}"
}






