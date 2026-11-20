package pl.espen.garett;

import java.text.Normalizer;
import java.util.Locale;

/**
 * Espen expressive pack: contextual reaction cues synthesised into the SAME
 * Opus-decoded PCM stream. One sound per TTS reply; no competing AudioTrack,
 * audio assets, network call or simultaneous microphone/speaker initialization.
 */
final class WatchFx {
    static final String[] NAMES={"fanfare","chuckle","surprise","oops","secret",
        "riddle","cuddle","magic","dance","eyeroll","victory","eureka",
        "suspense","prank","sad","sleepy","detective","robot","drums","nod"};
    private static final String[][] TERMS={
        {"brawo","gratul","udalo","świetnie","swietnie"},
        {"haha","hehe","żart","zart","śmiesz","smiesz"},
        {"wow","niesamow","naprawdę","naprawde","oho"},
        {"ups","oj","pomył","pomyl","przeprasz"},
        {"sekret","cicho","szep","tajemnic"},
        {"zagadka","zgadnij","pomyśl","pomysl"},
        {"przytul","kocham","serdusz"},
        {"magia","czar","abrakadabra"},
        {"tańcz","tancz","zatańcz","zatancz"},
        {"serio","oczywiście","oczywiscie"},
        {"zwycię","zwycie","wygran","hurra"},
        {"eureka","odkryłem","odkrylam","wiem już","wiem juz"},
        {"uwaga","napięci","napieci","chwilecz"},
        {"psikus","nabral","nabrał","żarcik","zarcik"},
        {"smutno","przykro","niestety"},
        {"dobranoc","śpij","spij","zasyp"},
        {"detektyw","śledzt","sledzt","trop"},
        {"robot","komputer","procesor"},
        {"werble","bęben","beben","brawa"},
        {"zgadzam","tak jest","potwierdz"}
    };
    static String classify(String sentence){
        if(sentence==null||sentence.isEmpty())return "";
        String s=Normalizer.normalize(sentence.toLowerCase(Locale.ROOT),Normalizer.Form.NFD)
            .replaceAll("\\p{M}+","").replace('ł','l');
        for(int i=0;i<TERMS.length;i++)
            for(String term:TERMS[i]) {
                String t=Normalizer.normalize(term.toLowerCase(Locale.ROOT),Normalizer.Form.NFD)
                    .replaceAll("\\p{M}+","").replace('ł','l');
                if(s.contains(t))return NAMES[i];
            }
        return "";
    }
    private String pending="";
    private short[] tone;
    private int position;
    private boolean alreadyUsed;
    synchronized void reset(){pending="";tone=null;position=0;alreadyUsed=false;}
    synchronized String cue(String sentence){
        if(alreadyUsed)return "";
        String name=classify(sentence);
        if(!name.isEmpty()){pending=name;alreadyUsed=true;}
        return name;
    }
    synchronized void mix(short[] pcm,int count,int rate){
        if(tone==null&&!pending.isEmpty()){
            tone=generate(pending,rate);pending="";position=0;
        }
        if(tone==null)return;
        int n=Math.min(count,tone.length-position);
        for(int i=0;i<n;i++){
            int v=pcm[i]+tone[position+i];
            pcm[i]=(short)Math.max(Short.MIN_VALUE,Math.min(Short.MAX_VALUE,v));
        }
        position+=n;if(position>=tone.length){tone=null;position=0;}
    }
    private static short[] generate(String name,int rate){
        int index=0;for(int i=0;i<NAMES.length;i++)if(NAMES[i].equals(name)){index=i;break;}
        final int length=Math.max(1,rate*260/1000);
        short[] sound=new short[length];
        int root=290+index*27;
        for(int i=0;i<length;i++){
            double t=i/(double)rate;
            double env=Math.pow(Math.max(0,1-t/0.26),2)*Math.min(1,t*100);
            double step=(i*3L/Math.max(1,length)); // 3-note melodic signature
            double freq=root*(step==0?1:step==1?1.25:1.5);
            double wave=Math.sin(2*Math.PI*freq*t);
            if(index==1||index==13)wave*=0.65+0.35*Math.sin(2*Math.PI*14*t);
            if(index==18)wave*=Math.sin(2*Math.PI*32*t);
            if(index==14||index==15)wave*=0.5;
            sound[i]=(short)(wave*env*2400); // well below TTS (~ -22dBFS)
        }
        return sound;
    }
}
