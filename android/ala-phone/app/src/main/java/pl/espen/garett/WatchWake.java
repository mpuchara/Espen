package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.SystemClock;
import android.util.Base64;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Optional, small-footprint local, trainable acoustic wake template for Android
 * 8.1. No SpeechRecognizer, speech-to-text, upload, background service or cloud.
 * Similar sounding phrases can still match: training needs three repetitions.
 */
final class WatchWake {
    interface Events { void onWake(); void message(String text); }
    private static final int RATE=16000, BLOCK=320, FEATURES=5;
    private final Activity activity;
    private final Events events;
    private volatile boolean running;
    private Thread thread;
    private final List<float[][]> patterns=new ArrayList<>();
    private boolean training;
    WatchWake(Activity a,Events e){activity=a;events=e;readSaved();}
    synchronized int trained(){return patterns.size();}
    private SharedPreferences prefs(){return activity.getSharedPreferences("watch_wake",Context.MODE_PRIVATE);}
    synchronized void forget(){stop();patterns.clear();prefs().edit().remove("templates").apply();}
    synchronized void train(){
        stop();
        if(activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            events.message("Przyznaj uprawnienie mikrofonu, zanim nauczysz hasła");return;
        }
        if(patterns.size()>=3){patterns.clear();prefs().edit().remove("templates").apply();}
        training=true;start(true);
    }
    synchronized void listen(){
        stop();
        if(patterns.size()<3)return;
        if(activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED)return;
        training=false;start(false);
    }
    synchronized void stop(){
        running=false;Thread t=thread;
        if(t!=null)t.interrupt();
        thread=null;
    }
    private synchronized void start(boolean enrollment){
        if(running)return;
        running=true;
        thread=new Thread(()->run(enrollment),enrollment?"ala-wake-learn":"ala-wake-local");
        thread.setDaemon(true);thread.start();
    }
    private void run(boolean enrollment){
        AudioRecord rec=null;
        try{
            int min=AudioRecord.getMinBufferSize(RATE,AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
            if(min<=0)throw new IllegalStateException("AudioRecord 16 kHz unsupported");
            rec=new AudioRecord(MediaRecorder.AudioSource.MIC,RATE,
                AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,
                Math.max(min,BLOCK*12));
            if(rec.getState()!=AudioRecord.STATE_INITIALIZED)throw new IllegalStateException("MIC failed");
            rec.startRecording();
            if(rec.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)
                throw new IllegalStateException("MIC not recording");
            events.message(enrollment?"Powiedz wybrane hasło wybudzenia ("+(trained()+1)+"/3)":"Ala czeka na lokalne hasło");
            List<float[]> candidate=new ArrayList<>();
            short[] pcm=new short[BLOCK];
            long begin=SystemClock.elapsedRealtime();
            int quiet=0;
            double background=115;
            while(running){
                int got=0;
                while(got<BLOCK && running){
                    int n=rec.read(pcm,got,BLOCK-got);
                    if(n<=0)throw new IllegalStateException("MIC.read="+n);
                    got+=n;
                }
                if(!running)break;
                double sum=0;for(short s:pcm)sum+=(long)s*s;
                double rms=Math.sqrt(sum/BLOCK);
                boolean speech=rms>Math.max(275,background*2.7);
                if(candidate.isEmpty()&&!speech){
                    background=background*.97+rms*.03;
                }else{
                    if(!speech)quiet++;else quiet=0;
                    if(candidate.size()<150)candidate.add(features(pcm,rms));
                }
                if(!candidate.isEmpty() && (quiet>=15||candidate.size()==150)){
                    int meaningful=candidate.size()-quiet;
                    if(meaningful>=20 && meaningful<=125){
                        float[][] sample=candidate.subList(0,meaningful).toArray(new float[0][]);
                        if(enrollment){
                            synchronized(this){patterns.add(sample);save();}
                            events.message("Próbka zapisana ("+trained()+"/3). "+(trained()<3?
                                "Naciśnij Naucz hasła ponownie.":"Gotowe! Hasło działa w trybie bez odbioru."));
                            break;
                        }else if(matches(sample)){
                            running=false;events.onWake();break;
                        }
                    }
                    candidate.clear();quiet=0;
                }
                if(enrollment && SystemClock.elapsedRealtime()-begin>7000){
                    events.message("Nie usłyszałam hasła. Spróbuj ponownie.");break;
                }
            }
        }catch(Throwable e){events.message("Lokalne hasło: "+e.getClass().getSimpleName());}
        finally{
            if(rec!=null){try{rec.stop();}catch(Exception ignored){}
                try{rec.release();}catch(Exception ignored){}}
            running=false;
        }
    }
    private static float[] features(short[] pcm,double rms){
        int flips=0;
        double[] frequencies={440,1000,2100};
        double[] energy=new double[3];
        for(int i=1;i<pcm.length;i++)if((pcm[i]>=0)!=(pcm[i-1]>=0))flips++;
        for(int k=0;k<3;k++){
            double coeff=2*Math.cos(2*Math.PI*frequencies[k]/RATE);
            double a=0,b=0,c;
            for(short sample:pcm){c=sample+coeff*a-b;b=a;a=c;}
            energy[k]=Math.max(1,a*a+b*b-coeff*a*b);
        }
        double total=energy[0]+energy[1]+energy[2]+1;
        return new float[]{(float)Math.min(1,Math.log1p(rms)/10),
            flips/(float)pcm.length,(float)(energy[0]/total),
            (float)(energy[1]/total),(float)(energy[2]/total)};
    }
    private synchronized boolean matches(float[][] sample){
        if(patterns.size()!=3)return false;
        double sum=0,max=0;
        for(float[][] target:patterns){
            double score=distance(sample,target);
            sum+=score;max=Math.max(max,score);
        }
        // Three agreeing templates + calibrated threshold guard against accidental wake.
        double trainedScore=0;
        trainedScore+=distance(patterns.get(0),patterns.get(1));
        trainedScore+=distance(patterns.get(0),patterns.get(2));
        trainedScore+=distance(patterns.get(1),patterns.get(2));
        double threshold=Math.min(.20,Math.max(.075,trainedScore/3*1.25+.02));
        return sum/3<threshold && max<threshold*1.4;
    }
    static double distance(float[][] a,float[][] b){
        int m=a.length,n=b.length;if(m==0||n==0)return 999;
        double[] prev=new double[n+1],row=new double[n+1];
        java.util.Arrays.fill(prev,1e9);prev[0]=0;
        for(int i=1;i<=m;i++){
            java.util.Arrays.fill(row,1e9);
            for(int j=1;j<=n;j++){
                if(Math.abs(i/(double)m-j/(double)n)>.28)continue;
                double cost=0;
                for(int k=0;k<FEATURES;k++){
                    double d=a[i-1][k]-b[j-1][k];cost+=d*d;
                }
                row[j]=Math.sqrt(cost)+Math.min(prev[j],Math.min(row[j-1],prev[j-1]));
            }
            double[] x=prev;prev=row;row=x;
        }
        return prev[n]/Math.max(m,n);
    }
    private synchronized void save(){
        try{
            ByteArrayOutputStream buf=new ByteArrayOutputStream();
            DataOutputStream d=new DataOutputStream(buf);
            d.writeInt(patterns.size());
            for(float[][] p:patterns){
                d.writeInt(p.length);
                for(float[] row:p)for(float f:row)d.writeFloat(f);
            }
            d.flush();
            prefs().edit().putString("templates",Base64.encodeToString(buf.toByteArray(),Base64.NO_WRAP)).apply();
        }catch(Exception ignored){}
    }
    private synchronized void readSaved(){
        try{
            String encoded=prefs().getString("templates","");
            if(encoded.length()>16000)return;
            DataInputStream in=new DataInputStream(new ByteArrayInputStream(Base64.decode(encoded,Base64.DEFAULT)));
            int count=in.readInt();if(count<0||count>3)return;
            for(int i=0;i<count;i++){
                int len=in.readInt();if(len<20||len>125)throw new IllegalArgumentException("bad template");
                float[][] sample=new float[len][FEATURES];
                for(float[] row:sample)for(int j=0;j<FEATURES;j++)row[j]=in.readFloat();
                patterns.add(sample);
            }
        }catch(Exception ignored){patterns.clear();}
    }
}
