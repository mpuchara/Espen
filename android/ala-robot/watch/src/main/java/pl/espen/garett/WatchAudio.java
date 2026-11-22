package pl.espen.garett;

import android.app.Activity;
import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.util.concurrent.ArrayBlockingQueue;

import io.github.jaredmdobson.concentus.OpusDecoder;

/**
 * Dedicated, bounded audio output for low-memory Android 8.1 watches.
 * No decoding / blocking AudioTrack.write calls on the WebSocket or control thread.
 * Owns and releases the AudioTrack exclusively from its audio thread.
 */
@SuppressWarnings("deprecation")
final class WatchAudio {
    private static final String TAG = "AlaAudio";
    interface Reporter {
        void report(String message);
        void pcmLevel(int level);
        default void started(){}
        default void sentence(String value){}
    }

    private static final class Frame {
        final byte[] opus;
        final String sentence;
        Frame(byte[] opus,String sentence){this.opus=opus;this.sentence=sentence;}
    }

    private static final class Marker {
        final long start,end;
        final int level;
        final String sentence;
        Marker(long start,long end,int level,String sentence){
            this.start=start;this.end=end;this.level=level;this.sentence=sentence;
        }
    }

    private final class Playback implements Runnable {
        final ArrayBlockingQueue<Frame> frames=new ArrayBlockingQueue<>(160);
        final Thread thread=new Thread(this,"ala-audio-output");
        final int preferredRate,preroll;
        final Thread microphone;
        volatile boolean cancelled,failed,inFlight,sealed;
        volatile long received,written,heard,dropped;
        volatile String route="uruchamianie";

        Playback(int preferredRate,Thread microphone,int preroll){
            this.preferredRate=preferredRate;
            this.microphone=microphone;
            this.preroll=preroll;
            thread.setDaemon(true);
        }

        @Override public void run(){
            AudioTrack track=null;
            AudioManager manager=null;
            int savedMode=0;
            boolean savedSpeaker=false,focus=false,routeChanged=false;
            try {
                android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
                if (microphone!=null && microphone.isAlive()) {
                    microphone.join(1500);
                    if (microphone.isAlive()) throw new IllegalStateException("Mikrofon zajęty");
                }
                synchronized(outputLock) {
                    if (cancelled) return;
                    try {
                        manager=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
                        if (manager!=null) {
                            savedMode=manager.getMode();
                            savedSpeaker=manager.isSpeakerphoneOn();
                            focus=manager.requestAudioFocus(null,AudioManager.STREAM_MUSIC,
                                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
                            if (activity.getPackageName().equals("pl.espen.garett")) {
                                routeChanged=true;
                                manager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                                manager.setSpeakerphoneOn(true);
                            }
                        }
                        track=openTrack(preferredRate);
                        if (track==null) throw new IllegalStateException("Brak wyjścia audio");
                        route="Głośnik: "+track.getSampleRate()+" Hz";
                        play(track);
                    } finally {
                        if (track!=null) {
                            try { track.pause(); track.flush(); } catch (RuntimeException ignored) {}
                            track.release();
                        }
                        if (manager!=null) {
                            if (routeChanged) {
                                manager.setSpeakerphoneOn(savedSpeaker);
                                manager.setMode(savedMode);
                            }
                            if (focus) manager.abandonAudioFocus(null);
                        }
                    }
                }
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            } catch (Exception error) {
                failed=true;
                Log.e(TAG,"Audio pipeline failed",error);
                if (current==this && !cancelled) reporter.report("Błąd głośnika: "+error.getClass().getSimpleName());
            } finally {
                if (current==this && !cancelled) reporter.pcmLevel(0);
            }
        }

        private void play(AudioTrack track) throws Exception {
            int rate=track.getSampleRate();
            OpusDecoder decoder=new OpusDecoder(rate,1);
            java.util.ArrayDeque<Marker> markers=new java.util.ArrayDeque<>();
            short[] pcm=new short[rate/5];
            long queuedSamples=0;
            String spoken="";
            int shownLevel=0;
            boolean started=false;
            long bufferUntil=SystemClock.elapsedRealtime()+800;
            while (!cancelled && !sealed && frames.size()<preroll &&
                SystemClock.elapsedRealtime()<bufferUntil) Thread.sleep(15);
            if (cancelled) return;
            track.play();
            while (!cancelled) {
                long head=track.getPlaybackHeadPosition()&0xffffffffL;
                while (!markers.isEmpty() && head>=markers.peek().end) {
                    Marker finished=markers.remove();
                    heard++;
                    if (!started) { started=true; reporter.started(); }
                    if (!spoken.equals(finished.sentence)) {
                        spoken=finished.sentence;
                        reporter.sentence(spoken);
                    }
                }
                Marker active=markers.peek();
                int level=active!=null && head>active.start ? active.level : 0;
                if (active!=null && head>active.start) {
                    if (!started) { started=true; reporter.started(); }
                    if (!spoken.equals(active.sentence)) {
                        spoken=active.sentence;
                        reporter.sentence(spoken);
                    }
                }
                if (level!=shownLevel) { shownLevel=level; reporter.pcmLevel(level); }
                inFlight=true;
                Frame frame=frames.poll();
                if (frame!=null) {
                    int count=decoder.decode(frame.opus,0,frame.opus.length,pcm,0,pcm.length,false);
                    if (count<=0) throw new IllegalStateException("Nie można zdekodować Opus");
                    long sum=0;
                    for (int i=0;i<count;i++) sum+=(long)pcm[i]*pcm[i];
                    int power=Math.min(100,(int)(Math.sqrt(sum/(double)count)/95));
                    markers.add(new Marker(queuedSamples,queuedSamples+count,power,frame.sentence));
                    int offset=0;
                    while (offset<count && !cancelled) {
                        int n=track.write(pcm,offset,count-offset,AudioTrack.WRITE_NON_BLOCKING);
                        if (n<0) throw new IllegalStateException("AudioTrack.write="+n);
                        if (n==0) {
                            // Sample the actual playhead even while the device buffer is full.
                            long playing=track.getPlaybackHeadPosition()&0xffffffffL;
                            Marker at=markers.peek();
                            while (at!=null && playing>=at.end) {
                                markers.remove();
                                heard++;
                                at=markers.peek();
                            }
                            if (at!=null && playing>at.start) {
                                if (!started) { started=true; reporter.started(); }
                                if (!spoken.equals(at.sentence)) {
                                    spoken=at.sentence;
                                    reporter.sentence(spoken);
                                }
                                if (at.level!=shownLevel) { shownLevel=at.level; reporter.pcmLevel(shownLevel); }
                            }
                            Thread.sleep(10);
                        }
                        offset+=n;
                    }
                    queuedSamples+=count;
                    written++;
                }
                inFlight=false;
                if (frame==null) Thread.sleep(markers.isEmpty()?25:10);
            }
        }
    }

    private final Activity activity;
    private final Reporter reporter;
    private final Object outputLock=new Object();
    private volatile Playback current;

    WatchAudio(Activity activity,Reporter reporter){this.activity=activity;this.reporter=reporter;}

    void resetSession(){ stop(); current=null; }
    synchronized void start(int preferredRate,Thread microphone,int preroll){
        stop();
        current=new Playback(preferredRate,microphone,preroll);
        current.thread.start();
    }
    void offer(byte[] opus,String sentence){
        Playback output=current;
        if (output==null || output.cancelled) return;
        output.received++;
        if (!output.frames.offer(new Frame(opus,sentence))) output.dropped++;
    }
    void seal(){Playback out=current;if (out!=null) out.sealed=true;}
    long packetsReceived(){Playback out=current;return out==null?0:out.received;}
    long packetsPlayed(){Playback out=current;return out==null?0:out.heard;}
    long packetsDropped(){Playback out=current;return out==null?0:out.dropped;}
    String route(){Playback out=current;return out==null?"nie uruchomiony":out.route;}
    boolean drained(){
        Playback out=current;
        return out==null || out.failed || (out.frames.isEmpty() && !out.inFlight && out.heard==out.written);
    }
    boolean complete(){
        Playback out=current;
        return out!=null && !out.failed && out.heard>0 && out.heard==out.received && out.dropped==0;
    }
    synchronized void stop(){
        Playback out=current;
        if (out!=null) {
            out.cancelled=true;
            out.frames.clear();
            out.thread.interrupt();
        }
    }

    private static AudioTrack openTrack(int preferredRate){
        for (int rate:new int[]{preferredRate,48000,24000,16000}) {
            int minimum=AudioTrack.getMinBufferSize(rate,AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT);
            if (minimum<=0) continue;
            try {
                AudioTrack candidate=new AudioTrack(AudioManager.STREAM_MUSIC,rate,AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT,Math.max(minimum,rate/5),AudioTrack.MODE_STREAM);
                if (candidate.getState()==AudioTrack.STATE_INITIALIZED) return candidate;
                candidate.release();
            } catch (IllegalArgumentException error) { Log.w(TAG,"AudioTrack "+rate+" Hz",error); }
        }
        return null;
    }

    /**
     * Local sine-wave test, independent of Xiaozhi. Used to separate routing
     * failures from network/Opus failures on real Garett hardware.
     */
    static Thread testSpeaker(Activity activity,Reporter reporter,Runnable finished) {
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
                for(int block=0;block<45&&!Thread.currentThread().isInterrupted();block++){
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
                finished.run();
            }
        },"ala-speaker-test");
        t.setDaemon(true);
        t.start();
        return t;
    }
}
