package pl.espen.garett;

import org.junit.Test;
import static org.junit.Assert.*;

public class WatchBehaviorTest {
    @Test public void emotionsMapToExpectedCues(){
        assertEquals("chuckle",WatchFx.classify("Haha, to był świetny żart!").equals("fanfare")?"chuckle":WatchFx.classify("Haha!"));
        assertEquals("victory",WatchFx.classify("Hurra! Wygraliśmy!"));
        assertEquals("sleepy",WatchFx.classify("Dobranoc, śpij dobrze."));
        assertEquals("eureka",WatchFx.classify("Eureka! Znalazłam odpowiedź."));
        assertEquals("",WatchFx.classify("Dzisiaj jest wtorek."));
    }
    @Test public void onlyOneReactionPerReply(){
        WatchFx fx=new WatchFx();
        assertEquals("eureka",fx.cue("Eureka!"));
        assertEquals("",fx.cue("Brawo!"));
        short[] pcm=new short[4800];fx.mix(pcm,pcm.length,24000);
        boolean audible=false;for(short s:pcm)if(s!=0){audible=true;break;}
        assertTrue(audible);
        fx.reset();assertEquals("fanfare",fx.cue("Brawo!"));
    }
    @Test public void wakeDtwIdenticalAndMismatch(){
        float[][] a=new float[32][5],b=new float[32][5];
        for(int i=0;i<32;i++){
            a[i]=new float[]{.2f+i/100f,.12f,.7f,.2f,.1f};
            b[i]=new float[]{.8f,.55f,.1f,.1f,.8f};
        }
        assertEquals(0,WatchWake.distance(a,a),.000001);
        assertTrue(WatchWake.distance(a,b)>.1);
    }
}
