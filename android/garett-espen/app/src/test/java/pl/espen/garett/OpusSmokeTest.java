package pl.espen.garett;

import org.junit.Test;
import static org.junit.Assert.*;
import io.github.jaredmdobson.concentus.OpusApplication;
import io.github.jaredmdobson.concentus.OpusEncoder;
import io.github.jaredmdobson.concentus.OpusDecoder;

public class OpusSmokeTest {
    private static void roundTrip(int frameMs, int complexity) throws Exception {
        int rate=16000;
        int samples=rate*frameMs/1000;
        OpusEncoder encoder=new OpusEncoder(rate,1,OpusApplication.OPUS_APPLICATION_VOIP);
        encoder.setBitrate(20000);
        encoder.setComplexity(complexity);
        OpusDecoder decoder=new OpusDecoder(24000,1);
        short[] pcm=new short[samples];
        byte[] output=new byte[1600];
        short[] decoded=new short[24000/5];
        for(int frame=0;frame<30;frame++){
            for(int i=0;i<samples;i++){
                float x=(frame*samples+i)/(float)rate;
                pcm[i]=(short)(10000*Math.sin(2*Math.PI*220*x));
            }
            int n=encoder.encode(pcm,0,samples,output,0,output.length);
            assertTrue("Opus failed to encode frame "+frame+", size="+frameMs,n>0);
            int count=decoder.decode(output,0,n,decoded,0,decoded.length,false);
            assertEquals(frameMs*24,count);
        }
    }
    @Test public void opus60msComplexity1() throws Exception {roundTrip(60,1);}
    @Test public void opus20msComplexity1() throws Exception {roundTrip(20,1);}
    @Test public void opus60msDefaultComplexity() throws Exception {roundTrip(60,10);}
}
