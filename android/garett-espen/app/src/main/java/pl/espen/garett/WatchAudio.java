package pl.espen.garett;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import io.github.jaredmdobson.concentus.OpusDecoder;

/**
 * Dedicated, bounded audio output for low-memory Android 8.1 watches.
 * No decoding / blocking AudioTrack.write calls on the WebSocket or control thread.
 * Owns and releases the AudioTrack exclusively from its audio thread.
 */
@SuppressWarnings("deprecation")
final class WatchAudio {
    private static final String TAG = "AlaAudio";
    interface Reporter { void report(String message); void pcmLevel(int level); }
    private final Activity activity;
    private final Reporter reporter;
    private final ArrayBlockingQueue<byte[]> frames = new ArrayBlockingQueue<>(24);
    private final AtomicBoolean running = new AtomicBoolean();
    private volatile Thread player;
    private volatile long lastWrittenMs, received, played, dropped;
    private volatile boolean soundConfigured;
    private volatile String route = "";
    private volatile int playbackHz = 48000;
    private volatile boolean diagnostic;
    private AudioManager manager;
    private int savedMode, savedVolume, savedSpeaker;
    private boolean focus;

    WatchAudio(Activity a, Reporter r) { activity=a; reporter=r; }

    void start() {
        stop();
        frames.clear();
        received=played=dropped=0;
        lastWrittenMs=0;
        diagnostic=false;
        running.set(true);
        Thread t=new Thread(this::playLoop, "ala-audio-output");
        t.setDaemon(true);player=t;t.start();
    }

    void offer(byte[] packet) {
        if(!running.get()||packet.length==0)return;
        received++;
        if(!frames.offer(packet)) {
            // Drop oldest for bounded memory, rather than accumulating unbounded
            // WebSocket callbacks and getting killed by low-memory Android 8.1.
            frames.poll();
            if(!frames.offer(packet))dropped++;
            else dropped++;
            if(dropped==1 || dropped%20==0)
                reporter.report("Dźwięk: wolny dekoder ("+dropped+" pominiętych ramek)");
        }
    }

    long packetsReceived(){ return received; }
    long packetsPlayed(){ return played; }
    long packetsDropped(){ return dropped; }
    boolean ready(){return soundConfigured;}
    String route(){return route;}
    boolean drained(){
        // A short last-buffer guard covers AudioTrack's hardware buffering.
        return frames.isEmpty() && (lastWrittenMs==0 || SystemClock.elapsedRealtime()-lastWrittenMs>850);
    }
    void stop(){
        running.set(false);frames.clear();
        Thread t=player;
        if(t!=null)t.interrupt();
        player=null;
    }
    private void playLoop(){
        android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
        AudioTrack track=null;
        try{
            manager=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
            if(manager!=null){
                savedMode=manager.getMode();
                savedVolume=manager.getStreamVolume(AudioManager.STREAM_MUSIC);
                savedSpeaker=manager.isSpeakerphoneOn()?1:0;
                manager.requestAudioFocus(null, AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN_TRANSIENT);
                focus=true;
                // On phone-watch firmware voice is frequently routed to
                // an earpiece unless communication speakerphone is requested.
                manager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                manager.setSpeakerphoneOn(true);
                if(savedVolume==0){
                    int max=manager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
                    manager.setStreamVolume(AudioManager.STREAM_MUSIC,Math.max(1,max*3/5),0);
                    reporter.report("Głośność multimediów była 0 – ustawiono 60%");
                }
            }
            // Decoding Opus at 48 kHz is supported even if Xiaozhi announces 24 kHz:
            // the decoded PCM duration is unchanged; avoids unsupported 24k audio HALs.
            int[] choices={24000,48000,16000};
            int stream=AudioManager.STREAM_MUSIC;
            for(int hz:choices){
                try{
                    int minimum=AudioTrack.getMinBufferSize(hz,
                        AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT);
                    if(minimum<=0)continue;
                    AudioTrack candidate=new AudioTrack(stream,hz,
                        AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT,
                        Math.max(minimum,hz/2),AudioTrack.MODE_STREAM);
                    if(candidate.getState()!=AudioTrack.STATE_INITIALIZED){candidate.release();continue;}
                    track=candidate;playbackHz=hz;route="Głośnik: "+hz+" Hz / media / speaker ON";
                    break;
                }catch(RuntimeException ex){Log.w(TAG,"AudioTrack "+hz+" Hz",ex);}
            }
            if(track==null)throw new IllegalStateException("Brak wyjścia audio Android 8.1");
            final OpusDecoder decoder=new OpusDecoder(playbackHz,1);
            track.play();
            soundConfigured=true;
            reporter.report("Głośnik gotowy ("+playbackHz+" Hz)");
            short[] pcm=new short[playbackHz/5];
            while(running.get()){
                byte[] frame=frames.poll(250,TimeUnit.MILLISECONDS);
                if(frame==null)continue;
                // Concentus is CPU-heavy on small watches. Keep every packet's
                // decoding and audio write off the UI and Xiaozhi control thread.
                int count=decoder.decode(frame,0,frame.length,pcm,0,pcm.length,false);
                if(count<=0)continue;
                long sum=0;for(int i=0;i<count;i++)sum+=(long)pcm[i]*pcm[i];
                reporter.pcmLevel(Math.min(100,(int)(Math.sqrt(sum/(double)count)/95)));
                int offset=0;
                while(offset<count&&running.get()){
                    int wrote=track.write(pcm,offset,count-offset,AudioTrack.WRITE_BLOCKING);
                    if(wrote<=0)throw new IllegalStateException("AudioTrack.write="+wrote);
                    offset+=wrote;
                }
                if(offset==count){played++;lastWrittenMs=SystemClock.elapsedRealtime();}
                if(played==1)reporter.report("Ala mówi przez głośnik");
            }
        }catch(InterruptedException ignored){Thread.currentThread().interrupt();}
        catch(Throwable ex){Log.e(TAG,"Audio pipeline failed",ex);
            reporter.report("Błąd głośnika: "+ex.getClass().getSimpleName()+
                (ex.getMessage()!=null?": "+ex.getMessage():""));
        }finally{
            soundConfigured=false;
            if(track!=null){
                try{track.pause();track.flush();track.release();}
                catch(Throwable ex){Log.w(TAG,"Release AudioTrack",ex);}
            }
            if(manager!=null){
                try {
                    manager.setSpeakerphoneOn(savedSpeaker!=0);
                    manager.setMode(savedMode);
                    if(focus)manager.abandonAudioFocus(null);
                }catch(Throwable ex){Log.w(TAG,"Restore audio route",ex);}
            }
            reporter.pcmLevel(0);
        }
    }

    /**
     * Local sine-wave test, independent of Xiaozhi. Used to separate routing
     * failures from network/Opus failures on real Garett hardware.
     */
    static void testSpeaker(Activity activity,Reporter reporter) {
        Thread t=new Thread(()->{
            android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
            AudioTrack track=null;
            AudioManager am=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
            int oldMode=am!=null?am.getMode():AudioManager.MODE_NORMAL;
            boolean oldSpeaker=am!=null&&am.isSpeakerphoneOn();
            try{
                if(am!=null){
                    am.setMode(AudioManager.MODE_IN_COMMUNICATION);
                    am.setSpeakerphoneOn(true);
                    int max=am.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
                    if(am.getStreamVolume(AudioManager.STREAM_MUSIC)==0)
                        am.setStreamVolume(AudioManager.STREAM_MUSIC,Math.max(1,max/2),0);
                }
                int sampleRate=16000;
                int min=AudioTrack.getMinBufferSize(sampleRate,
                    AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT);
                if(min<=0)throw new IllegalStateException("Brak wyjścia 16 kHz");
                track=new AudioTrack(AudioManager.STREAM_MUSIC,sampleRate,
                    AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(min,4096),AudioTrack.MODE_STREAM);
                track.play();
                short[] sound=new short[640]; // 40ms
                for(int block=0;block<45;block++){
                    for(int i=0;i<sound.length;i++){
                        double tsec=(block*sound.length+i)/(double)sampleRate;
                        double envelope=Math.min(1.0,Math.min(tsec*12,(1.8-tsec)*12));
                        sound[i]=(short)(Math.sin(2*Math.PI*660*tsec)*5500*Math.max(0,envelope));
                    }
                    track.write(sound,0,sound.length,AudioTrack.WRITE_BLOCKING);
                }
                reporter.report("Test zakończony: czy słychać 660 Hz?");
            }catch(Throwable ex){Log.e(TAG,"Test tone failed",ex);
                reporter.report("Test głośnika: "+ex.getClass().getSimpleName());}
            finally {
                if(track!=null){try{track.stop();track.release();}catch(Throwable ignored){}}
                if(am!=null){try{am.setSpeakerphoneOn(oldSpeaker);am.setMode(oldMode);}catch(Throwable ignored){}}
            }
        },"ala-speaker-test");
        t.setDaemon(true);t.start();
    }
}
