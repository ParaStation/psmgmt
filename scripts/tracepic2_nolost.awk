#!/usr/bin/gawk -f 
BEGIN{ 
 kcnt=0;seq=0;lostcnt=0;lcnt=0;
 maxlcnt=7;
 maxvalcnt=5;
}



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
#	    enu[kcnt]=f;
#	    kcnt++;
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
#       Write 8 posible Values:	
	if ((infos[f,$6]==0)&&(infos[f,"cnt"]<maxvalcnt)){
	    infos[f,$6]=1;
	    infos[f,"cnt"]++;
	    infos[f]=infos[f]"\\nVal="$6;
	    if (infos[f,"cnt"]==maxvalcnt){
	     infos[f]=infos[f]"\\n:";
	    }    
	}
	count[f]++;
	
	time=$5-lasttime;
	if (time < 0) { time += 4294967296}
	ltimes[lastlink,f]+=time;
	lcount[lastlink,f]++;
	lcnt++;
	if (lcount[lastlink,f]<maxlcnt){
	 if (lcount[lastlink,f]>1){
	  lcount[lastlink,f,"tr"]=lcount[lastlink,f,"tr"]","lcnt;
	 }else{
	  lcount[lastlink,f,"tr"]=lcnt;
	 }
	}
	if (lcount[lastlink,f]==maxlcnt){
	 lcount[lastlink,f,"tr"]=lcount[lastlink,f,"tr"]"...";
	}
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
		tri=lcount[enu[i],enu[j],"tr"];
		print "knote"i "->" "knote"j "\t[label=\"#"lcount[enu[i],enu[j]]"*"zeit"\\n"tri"\"]"
	    }
	}
    }
    print "}"
}






