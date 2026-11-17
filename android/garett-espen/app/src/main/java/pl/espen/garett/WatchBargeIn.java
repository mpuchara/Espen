package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;

/**
 * Optional guarded barge-in on vendor audio HALs that support simultaneous
 * PCM input/output. Never required for normal conversations or touch interrupt.
 * Exits quietly if the watch refuses a second audio stream.
 */
final class WatchBargeIn {
    interface Callback {
        boolean canMonitor();
        int outputLevel();
        void interrupted();
        void unavailable(String why);
    }
    private final Activity a;
    private final Callback cb;
    private volatile boolean running;
    private volatile boolean unavailable;
    private Thread thread;
    WatchBargeIn(Activity a,Callback cb){this.a=a;this.cb=cb;}
    synchronized void start(){
        if(running||unavailable||a.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED)return;
        running=true;thread=new Thread(this::monitor,"ala-barge-in");
        thread.setDaemon(true);thread.start();
    }
    synchronized void reset(){unavailable=false;stop();}
    synchronized void stop(){
        running=false;
        if(thread!=null)thread.interrupt();
        thread=null;
    }
    private void monitor(){
        AudioRecord record=null;
        try{
            int rate=16000,size=320;
            int min=AudioRecord.getMinBufferSize(rate,AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
            if(min<=0)throw new IllegalStateException("No 16 kHz input");
            record=new AudioRecord(MediaRecorder.AudioSource.MIC,rate,
                AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,Math.max(min,size*10));
            if(record.getState()!=AudioRecord.STATE_INITIALIZED)throw new IllegalStateException("Concurrent MIC unavailable");
            record.startRecording();
            if(record.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)throw new IllegalStateException("Concurrent MIC failed");
            short[] pcm=new short[size];
            int consecutive=0,frames=0;
            while(running&&cb.canMonitor()){
                int off=0;
                while(off<size&&running){
                    int n=record.read(pcm,off,size-off);
                    if(n<=0)throw new IllegalStateException("Concurrent MIC read="+n);
                    off+=n;
                }
                if(!running)break;
                double sum=0;for(short v:pcm)sum+=(long)v*v;
                double rms=Math.sqrt(sum/size);
                // Echo guard: the local voice must substantially exceed the
                // measured playback envelope. Never trigger from isolated peaks.
                int reference=cb.outputLevel();
                double threshold=Math.max(2500,reference*165.0);
                if(++frames>20&&rms>threshold){consecutive++;}
                else consecutive=Math.max(0,consecutive-2);
                if(consecutive>=16){
                    running=false;cb.interrupted();break;
                }
            }
        }catch(Throwable e){
            if(running){unavailable=true;cb.unavailable(e.getClass().getSimpleName());}
        }finally{
            if(record!=null){try{record.stop();}catch(Throwable ignored){}
                try{record.release();}catch(Throwable ignored){}}
            running=false;
        }
    }
}
