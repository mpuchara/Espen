package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.NoiseSuppressor;
import android.os.Build;
import android.os.SystemClock;
import io.github.jaredmdobson.concentus.OpusApplication;
import io.github.jaredmdobson.concentus.OpusDecoder;
import io.github.jaredmdobson.concentus.OpusEncoder;
import java.io.IOException;
import java.security.SecureRandom;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okio.ByteString;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Xiaozhi OTA + protocol-v1 WebSocket + raw Opus; all network I/O on worker threads. */
public final class XiaozhiClient {
    public interface Events {
        void state(FaceView.State state);
        void status(String text);
        void caption(String text);
        void emotion(String emotion);
        void level(int value, boolean microphone);
        void activation(String code);
    }
    public static final class Identity {
        public final String deviceId, clientId;
        Identity(String d,String c){deviceId=d;clientId=c;}
    }
    private static final String DEFAULT_OTA="https://api.tenclass.net/xiaozhi/ota/";
    private static final MediaType JSON=MediaType.parse("application/json; charset=utf-8");
    private final Activity activity;
    private final Events events;
    private final Vision vision;
    private final WatchAudio audio;
    private final ScheduledExecutorService worker=Executors.newSingleThreadScheduledExecutor();
    private final AtomicBoolean otaBusy=new AtomicBoolean();
    private final OkHttpClient http=new OkHttpClient.Builder()
        .connectTimeout(10,TimeUnit.SECONDS)
        .readTimeout(15,TimeUnit.SECONDS)
        .writeTimeout(15,TimeUnit.SECONDS)
        .pingInterval(20,TimeUnit.SECONDS).build();
    private volatile WebSocket socket;
    private volatile boolean recording, closed, sessionStarting, sessionReady, talking;
    private Thread recordThread;
    private volatile long lastAudioAt;
    private volatile long micSentFrames;
    private volatile int peakMicRms, sttResponses, ttsResponses;
    private volatile String micStage="idle";
    private volatile String sessionId="";
    private String wsUrl, wsToken, legacyVisionUrl, legacyVisionToken;
    private int wsVersion=1, outRate=24000;
    private long ttsStoppedAt=0;
    private final Identity identity;

    public XiaozhiClient(Activity a, Events listener) {
        activity=a;events=listener;identity=id(a);vision=new Vision(a,http);
        vision.setEnabled(prefs(a).getBoolean("vision",true)); // enabled by default, explicit voice request required before upload
        audio=new WatchAudio(a,new WatchAudio.Reporter(){
            @Override public void report(String message){events.status(message);}
            @Override public void pcmLevel(int value){events.level(value,false);}
        });
    }
    private static SharedPreferences prefs(Context c){return c.getSharedPreferences("espen_watch_v1",Context.MODE_PRIVATE);}
    public static Identity id(Context c){
        SharedPreferences p=prefs(c);
        String d=p.getString("device_id",""),u=p.getString("client_id","");
        if(d.isEmpty()){
            byte[] bytes=new byte[6];new SecureRandom().nextBytes(bytes);
            bytes[0]=(byte)((bytes[0]&0xfc)|0x02); // stable locally-administered, unicast ID
            d=String.format(Locale.ROOT,"%02x:%02x:%02x:%02x:%02x:%02x",
                bytes[0]&255,bytes[1]&255,bytes[2]&255,bytes[3]&255,bytes[4]&255,bytes[5]&255);
            p.edit().putString("device_id",d).apply();
        }
        if(u.isEmpty()){u=UUID.randomUUID().toString();p.edit().putString("client_id",u).apply();}
        return new Identity(d,u);
    }
    public static String getOta(Context c){return prefs(c).getString("ota",DEFAULT_OTA);}
    public void setOta(String url){
        if(!url.startsWith("https://")||url.length()>2048)throw new IllegalArgumentException("OTA musi używać HTTPS");
        prefs(activity).edit().putString("ota",url).apply();
        wsUrl=null;wsToken=null;
        stop();
        refreshConfig();
    }
    public void setVisionEnabled(boolean enable){
        prefs(activity).edit().putBoolean("vision",enable).apply();vision.setEnabled(enable);
    }
    public boolean isVisionEnabled(){return vision.isEnabled();}
    public void testSpeaker(){
        if(socket!=null||sessionStarting){events.status("Zakończ rozmowę przed testem dźwięku");return;}
        WatchAudio.testSpeaker(activity,new WatchAudio.Reporter(){
            @Override public void report(String message){events.status(message);}
            @Override public void pcmLevel(int value){events.level(value,false);}
        });
    }
    public void testMicrophone(){
        if(socket!=null||recording||sessionStarting){
            events.status("Najpierw zakończ rozmowę z Xiaozhi");return;
        }
        if(activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            events.status("Nadaj Ali uprawnienie do mikrofonu");return;
        }
        new Thread(()->{
            AudioRecord input=null;
            String stage="microphone.newRecord";
            String result="";
            try{
                final int rate=16000,samples=960;
                int buffer=AudioRecord.getMinBufferSize(rate,AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_16BIT);
                if(buffer<=0)throw new IOException("Brak 16 kHz, kod="+buffer);
                input=new AudioRecord(MediaRecorder.AudioSource.MIC,rate,
                    AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,
                    Math.max(buffer,samples*8));
                if(input.getState()!=AudioRecord.STATE_INITIALIZED)
                    throw new IOException("AudioRecord nie jest gotowy");
                stage="microphone.start";
                input.startRecording();
                if(input.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)
                    throw new IOException("AudioRecord nie nagrywa");
                stage="microphone.read";
                short[] pcm=new short[samples];
                int reads=0,maxRms=0;
                for(int pass=0;pass<16;pass++){
                    int offset=0;
                    while(offset<samples){
                        int n=input.read(pcm,offset,samples-offset);
                        if(n<=0)throw new IOException("AudioRecord.read="+n);
                        offset+=n;
                    }
                    reads++;
                    long sum=0;for(short x:pcm)sum+=(long)x*x;
                    maxRms=Math.max(maxRms,(int)Math.sqrt(sum/(double)samples));
                }
                stage="opus.init";
                OpusEncoder encoder=new OpusEncoder(rate,1,OpusApplication.OPUS_APPLICATION_VOIP);
                encoder.setBitrate(20000);encoder.setComplexity(1);
                stage="opus.encode";
                byte[] output=new byte[1600];
                int encoded=encoder.encode(pcm,0,samples,output,0,output.length);
                if(encoded<=0)throw new IOException("Opus.encode="+encoded);
                result="Mikrofon OK: "+reads+" ramek, RMS max "+maxRms+
                    ", Opus "+encoded+" bajtów.";
            }catch(Throwable error){
                CrashNotes.record(activity,"MIC TEST / "+stage,error);
                result="Test nieudany na etapie "+stage+" ("+
                    error.getClass().getSimpleName()+"). Kopiuj diagnostykę.";
            }finally{
                if(input!=null){try{input.stop();}catch(Throwable ignored){}
                    try{input.release();}catch(Throwable ignored){}}
            }
            final String feedback=result;
            events.status(feedback);
            activity.runOnUiThread(()->{
                if(!activity.isFinishing()){
                    new android.app.AlertDialog.Builder(activity)
                        .setTitle("Test mikrofonu Android 8.1")
                        .setMessage(feedback)
                        .setPositiveButton("OK",null).show();
                }
            });
        },"ala-mic-self-test").start();
    }
    public String audioDiagnostics(){
        return "Mikrofon: pakiety Opus wysłane "+micSentFrames+
            ", RMS max "+peakMicRms+", etap "+micStage+
            "\\nXiaozhi: STT "+sttResponses+", TTS "+ttsResponses+
            "\\nGłośnik: "+audio.route()+
            "\\nPakiety TTS: "+audio.packetsReceived()+
            ", odtworzone: "+audio.packetsPlayed()+", pominięte: "+audio.packetsDropped();
    }
    public void previewCamera(){
        vision.preview((message,error)->{
            if(error!=null)events.status(error);else events.status(message);
        });
    }
    public void refreshConfig(){
        if(closed||!otaBusy.compareAndSet(false,true))return;
        worker.execute(()->{
            try{readOta();}
            catch(Exception e){events.status("Xiaozhi OTA: "+brief(e));}
            finally{otaBusy.set(false);}
        });
    }
    private void scheduleOtaPoll(){
        if(!closed)worker.schedule(this::refreshConfig,12,TimeUnit.SECONDS);
    }
    private void readOta() throws Exception {
        String ota=getOta(activity);
        if(!ota.startsWith("https://"))throw new IOException("Odmówiono nieszyfrowanego OTA");
        JSONObject body=new JSONObject().put("version",2)
            .put("language","pl-PL").put("minimum_free_heap_size",0)
            .put("mac_address",identity.deviceId).put("uuid",identity.clientId)
            .put("chip_model_name","android")
            .put("application",new JSONObject().put("name","garett-espen")
                .put("version","0.1.3").put("idf_version","android-"+Build.VERSION.SDK_INT)
                .put("compile_time","").put("elf_sha256",""))
            .put("board",new JSONObject().put("type","garett-essa2-android")
                .put("name",Build.MODEL).put("manufacturer",Build.MANUFACTURER)
                .put("mac",identity.deviceId));
        Request request=new Request.Builder().url(ota)
            .header("Device-Id",identity.deviceId)
            .header("Client-Id",identity.clientId)
            .header("Activation-Version","1")
            .header("Accept-Language","pl-PL")
            .header("User-Agent","garett-espen/0.1.3")
            .post(RequestBody.create(JSON,body.toString())).build();
        JSONObject result;
        try(Response response=http.newCall(request).execute()){
            if(!response.isSuccessful()||response.body()==null)
                throw new IOException("HTTP "+response.code());
            result=new JSONObject(response.body().string());
        }
        JSONObject activation=result.optJSONObject("activation");
        if(activation!=null&&!activation.optString("code","").isEmpty()){
            String code=activation.optString("code");
            events.activation(code);
            events.status("Dodaj zegarek do agenta Ala w Xiaozhi");
            if(!activation.optString("challenge","").isEmpty())checkActivation(ota);
            scheduleOtaPoll();
            return;
        }
        // If server provides both MQTT and WebSocket, prefer WebSocket.
        JSONObject ws=result.optJSONObject("websocket");
        if(ws==null||ws.optString("url","").isEmpty()){
            events.activation("");
            throw new IOException("Konto nie udostępnia WebSocket (tylko MQTT?)");
        }
        String endpoint=ws.optString("url","");
        if(!endpoint.startsWith("wss://"))throw new IOException("Wymagany szyfrowany WebSocket wss://");
        int version=ws.optInt("version",1);
        if(version!=1)throw new IOException("Ten serwer wymaga binarnego protokołu "+version+" (MVP używa v1)");
        wsUrl=endpoint;wsToken=ws.optString("token","");wsVersion=version;
        events.activation("");events.status("Gotowa. Dotknij mikrofonu!");
    }
    private void checkActivation(String ota){
        try{
            String activationUrl=ota.endsWith("/")?ota+"activate":ota+"/activate";
            Request request=new Request.Builder().url(activationUrl)
                .header("Device-Id",identity.deviceId).header("Client-Id",identity.clientId)
                .header("Activation-Version","1")
                .post(RequestBody.create(JSON,"{}")).build();
            try(Response response=http.newCall(request).execute()){
                if(response.code()==200)events.status("Sparowano! Odświeżam Xiaozhi…");
            }
        }catch(Exception ignored){ /* OTA poll will retry; no credentials in logs. */ }
    }
    public void toggle(){
        if(socket!=null||sessionStarting){stop();return;}
        if(activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            events.status("Zezwól aplikacji na mikrofon");return;
        }
        micSentFrames=0;peakMicRms=0;sttResponses=0;ttsResponses=0;micStage="start";
        sessionStarting=true;events.state(FaceView.State.CONNECTING);events.status("Łączę z Alą…");
        worker.execute(()->{
            try {
                if(wsUrl==null)readOta();
                if(wsUrl==null){sessionStarting=false;events.state(FaceView.State.IDLE);return;}
                if(closed){sessionStarting=false;return;}
                openSocket();
            }catch(Exception e){sessionStarting=false;events.state(FaceView.State.ERROR);events.status("Połączenie: "+brief(e));}
        });
    }
    private void openSocket(){
        Request.Builder b=new Request.Builder().url(wsUrl)
            .header("Device-Id",identity.deviceId).header("Client-Id",identity.clientId)
            .header("Protocol-Version",Integer.toString(wsVersion));
        if(wsToken!=null&&!wsToken.isEmpty())b.header("Authorization",
            wsToken.startsWith("Bearer ")?wsToken:"Bearer "+wsToken);
        legacyVisionUrl=null;legacyVisionToken=null;sessionId="";
        socket=http.newWebSocket(b.build(),new WebSocketListener(){
            @Override public void onOpen(WebSocket webSocket,Response response){
                try{
                    JSONObject hello=new JSONObject().put("type","hello").put("version",1)
                        .put("transport","websocket").put("features",new JSONObject().put("mcp",true).put("aec",false))
                        .put("audio_params",new JSONObject().put("format","opus")
                            .put("sample_rate",16000).put("channels",1).put("frame_duration",60));
                    webSocket.send(hello.toString());
                }catch(Exception e){fail("Hello: "+brief(e));}
            }
            @Override public void onMessage(WebSocket webSocket,String text){
                worker.execute(()->parseMessage(webSocket,text));
            }
            @Override public void onMessage(WebSocket webSocket,ByteString bytes){
                if(webSocket==socket&&sessionReady)audio.offer(bytes.toByteArray());
            }
            @Override public void onFailure(WebSocket webSocket,Throwable t,Response response){
                if(webSocket==socket){socket=null;sessionStarting=false;endMic();events.state(FaceView.State.ERROR);events.status("Xiaozhi: "+brief(t));closeAudio();}
            }
            @Override public void onClosed(WebSocket webSocket,int code,String reason){
                if(webSocket==socket){socket=null;sessionStarting=false;endMic();events.state(FaceView.State.IDLE);closeAudio();}
            }
        });
    }
    private void parseMessage(WebSocket sender,String text){
        if(sender!=socket||closed)return;
        try{
            JSONObject o=new JSONObject(text);
            String type=o.optString("type","");
            switch(type){
                case "hello":
                    if(!"websocket".equals(o.optString("transport")))throw new IOException("Zły transport");
                    sessionId=o.optString("session_id","");
                    outRate=o.optJSONObject("audio_params")!=null?
                        o.optJSONObject("audio_params").optInt("sample_rate",24000):24000;
                    if(outRate!=16000&&outRate!=24000&&outRate!=48000)
                        throw new IOException("Nieobsługiwana częstotliwość TTS");
                    audio.start(); // lazy dedicated audio thread, no audio HAL on network control loop
                    sessionStarting=false;sessionReady=true;talking=false;
                    events.state(FaceView.State.LISTENING);events.status("Słucham…");
                    send(new JSONObject().put("type","listen").put("state","start").put("mode","manual"));
                    beginMic();
                    // Bounded one-turn session: no perpetual listening/CPU drain
                    // if the cloud never returns a TTS response.
                    worker.schedule(()->{
                        if(sender==socket&&sessionReady&&ttsStoppedAt==0&&!talking){
                            finishSession();
                            events.status("Xiaozhi nie odpowiedziało (45 s). Spróbuj ponownie.");
                        }
                    },45,TimeUnit.SECONDS);
                    break;
                case "stt":
                    sttResponses++;
                    String phrase=o.optString("text","");
                    vision.noteUserSpeech(phrase);
                    events.caption(phrase);
                    events.state(FaceView.State.THINKING);
                    events.status("Xiaozhi rozpoznało wypowiedź");
                    break;
                case "llm":
                    events.emotion(o.optString("emotion","neutral"));
                    break;
                case "tts":
                    String state=o.optString("state","");
                    if("start".equals(state)){
                        ttsResponses++;
                        talking=true;ttsStoppedAt=0;endMic();
                        events.state(FaceView.State.SPEAKING);
                        events.status("Odbieram odpowiedź głosową…");
                    }
                    else if("sentence_start".equals(state))events.caption(o.optString("text",""));
                    else if("stop".equals(state)){
                        talking=false;ttsStoppedAt=SystemClock.elapsedRealtime();
                        worker.schedule(this::finishAfterAudio,1200,TimeUnit.MILLISECONDS);
                    }
                    break;
                case "mcp": handleMcp(o.optJSONObject("payload"));break;
                case "alert":events.status(o.optString("message","Informacja od Xiaozhi"));break;
                default:break;
            }
        }catch(Throwable ex){fail("Protokół: "+brief(ex));}
    }
    private void send(JSONObject body){
        try{
            if(!sessionId.isEmpty())body.put("session_id",sessionId);
            WebSocket s=socket;if(s!=null)s.send(body.toString());
        }catch(JSONException ignored){}
    }
    private void beginMic(){
        if(recording)return;
        recording=true;
        recordThread=new Thread(this::recordLoop,"espen-mic");
        recordThread.setDaemon(true);recordThread.start();
    }
    private void recordLoop(){
        AudioRecord record=null;
        try{
            int rate=16000,samples=960;
            micStage="audio.minBuffer";
            int min=AudioRecord.getMinBufferSize(rate,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT);
            if(min<=0)throw new IOException("Mikrofon 16 kHz niedostępny");
            // Android 8.1 watch HAL often rejects VOICE_COMMUNICATION + AEC/NS.
            // The plain MIC input avoids that path and is enough for Xiaozhi.
            micStage="audio.newRecord";
            record=new AudioRecord(MediaRecorder.AudioSource.MIC,rate,
                AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,Math.max(min,samples*8));
            if(record.getState()!=AudioRecord.STATE_INITIALIZED)
                throw new IOException("Nie można otworzyć mikrofonu (MIC 16 kHz)");
            micStage="opus.newEncoder";
            OpusEncoder encoder=new OpusEncoder(rate,1,OpusApplication.OPUS_APPLICATION_VOIP);
            micStage="opus.configure";
            encoder.setBitrate(20000);
            encoder.setComplexity(1); // limit CPU/stack pressure on watch
            short[] pcm=new short[samples];byte[] opus=new byte[4000];
            micStage="audio.startRecording";
            record.startRecording();
            if(record.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)
                throw new IOException("Android nie rozpoczął nagrywania");
            long began=SystemClock.elapsedRealtime(), lastSpeech=began;
            boolean heard=false;int consecutive=0;double noise=95;
            while(recording && socket!=null && sessionReady && !talking){
                micStage="audio.read";
                int off=0;
                while(off<samples && recording){
                    int count=record.read(pcm,off,samples-off);
                    if(count<=0)throw new IOException("AudioRecord.read: "+count);
                    off+=count;
                }
                if(!recording)break;
                long energy=0;for(short v:pcm){energy+=(long)v*v;}
                int rms=(int)Math.sqrt(energy/(double)samples);
                if(rms>peakMicRms)peakMicRms=rms;
                events.level(Math.min(100,rms/55),true);
                long now=SystemClock.elapsedRealtime();
                if(rms>Math.max(230,noise*2.0)){
                    if(++consecutive>=2){heard=true;lastSpeech=now;}
                }else{consecutive=0;if(!heard)noise=noise*.96+rms*.04;}
                micStage="opus.encode";
                int length=encoder.encode(pcm,0,samples,opus,0,opus.length);
                micStage="websocket.send";
                WebSocket s=socket;
                if(s!=null&&length>0){
                    if(s.queueSize()>250_000){events.status("Wolne łącze – wstrzymuję mikrofon");break;}
                    if(s.send(ByteString.of(opus,0,length)))micSentFrames++;
                }
                if((heard&&now-lastSpeech>1900)||(!heard&&now-began>8500))break;
            }
            if(recording&&socket!=null&&!talking){
                recording=false;
                micStage="finish";
                events.state(FaceView.State.THINKING);events.status("Ala myśli…");
                try{Thread.sleep(180);}catch(InterruptedException ignored){Thread.currentThread().interrupt();}
                send(new JSONObject().put("type","listen").put("state","stop"));
            }
        }catch(Throwable e){
            String stage=micStage;
            CrashNotes.record(activity,"MIC / "+stage,e);
            if(recording){
                recording=false;
                // A failed encoder previously left Xiaozhi waiting forever for
                // listen.stop, making the user think the loudspeaker was broken.
                try{send(new JSONObject().put("type","listen").put("state","stop"));}catch(Exception ignored){}
                events.status("Mikrofon ["+stage+"]: "+brief(e)+"; zobacz Diagnostyka");
                events.state(FaceView.State.ERROR);
            }
        }finally{
            recording=false;
            if(record!=null){try{record.stop();}catch(Throwable ignored){}
                try{record.release();}catch(Throwable ignored){}}
        }
    }
    private void endMic(){recording=false;}
    private void closeAudio(){audio.stop();}
    private void finishAfterAudio(){
        if(socket==null||ttsStoppedAt==0||talking)return;
        long elapsed=SystemClock.elapsedRealtime()-ttsStoppedAt;
        if(elapsed<13000 && !audio.drained()){
            worker.schedule(this::finishAfterAudio,400,TimeUnit.MILLISECONDS);
            return;
        }
        if(audio.packetsReceived()==0)
            events.status("Xiaozhi zakończyło TTS bez ramek audio; sprawdź usługę");
        finishSession();
    }
    private void finishSession(){
        if(closed)return;
        WebSocket s=socket;socket=null;sessionReady=false;sessionStarting=false;
        endMic();
        if(s!=null)s.close(1000,"Done");
        closeAudio();
        events.state(FaceView.State.IDLE);
        if(audio.packetsReceived()>0)
            events.status("Dotknij mikrofonu, by porozmawiać");
        else events.status("Sesja zakończona. Jeśli cisza: test głośnika w ustawieniach.");
    }
    public void stop(){worker.execute(this::finishSession);}
    private void fail(String why){
        finishSession();events.status(why);events.state(FaceView.State.ERROR);
    }
    private static String brief(Throwable t){
        String s=t.getMessage();if(s==null||s.length()==0)return t.getClass().getSimpleName();
        if(s.length()>110)s=s.substring(0,110);
        return s.replaceAll("(?i)bearer\\s+\\S+","Bearer [ukryty]");
    }

    // MCP: supports legacy initialize plus modern discover/list/call. Never sends a photo
    // merely because the server asks: requires explicit user speech + opt-in.
    private void handleMcp(JSONObject req) throws JSONException {
        if(req==null)return;
        Object id=req.opt("id");
        String method=req.optString("method","");
        JSONObject params=req.optJSONObject("params");
        if(params==null)params=new JSONObject();
        JSONObject meta=params.optJSONObject("_meta");
        boolean modern=meta!=null||"server/discover".equals(method);
        if("initialize".equals(method)){
            JSONObject capabilities=params.optJSONObject("capabilities");
            JSONObject settings=capabilities==null?null:capabilities.optJSONObject("vision");
            legacyVisionUrl=settings==null?"":settings.optString("url","");
            legacyVisionToken=settings==null?"":settings.optString("token","");
            if(id!=null&&id!=JSONObject.NULL){
                JSONObject result=new JSONObject().put("protocolVersion","2024-11-05")
                    .put("capabilities",new JSONObject().put("tools",new JSONObject()))
                    .put("serverInfo",serverInfo());
                mcpResult(id,result,false);
            }
            return;
        }
        if(id==null||id==JSONObject.NULL)return;
        if("server/discover".equals(method)){
            JSONObject caps=new JSONObject().put("tools",new JSONObject())
                .put("extensions",new JSONObject().put("me.xiaozhi/vision",new JSONObject()));
            JSONObject result=new JSONObject().put("supportedVersions",
                new JSONArray().put("2026-07-28").put("2024-11-05"))
                .put("capabilities",caps).put("ttlMs",60000).put("cacheScope","private");
            mcpResult(id,result,true);return;
        }
        if("tools/list".equals(method)){
            JSONObject camera=new JSONObject().put("name","self.camera.take_photo")
                .put("description","Zrób zdjęcie na wyraźne życzenie użytkownika i opisz je po polsku")
                .put("inputSchema",new JSONObject().put("type","object")
                    .put("properties",new JSONObject().put("question",
                        new JSONObject().put("type","string")))
                    .put("required",new JSONArray().put("question")));
            JSONObject volumeFields=new JSONObject().put("volume",new JSONObject()
                .put("type","integer").put("minimum",0).put("maximum",100));
            JSONObject volumeSchema=new JSONObject().put("type","object")
                .put("properties",volumeFields)
                .put("required",new JSONArray().put("volume"));
            JSONObject volume=new JSONObject().put("name","self.audio_speaker.set_volume")
                .put("description","Ustaw głośność zegarka (0–100)")
                .put("inputSchema",volumeSchema);
            mcpResult(id,new JSONObject().put("tools",new JSONArray().put(camera).put(volume)),modern);
            return;
        }
        if(!"tools/call".equals(method)){mcpError(id,-32601,"Nieznana metoda");return;}
        String tool=params.optString("name","");
        JSONObject args=params.optJSONObject("arguments");
        if(args==null)args=new JSONObject();
        if("self.audio_speaker.set_volume".equals(tool)){
            int v=Math.max(0,Math.min(100,args.optInt("volume",50)));
            AudioManager audio=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
            if(audio!=null){
                int max=audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
                audio.setStreamVolume(AudioManager.STREAM_MUSIC,max*v/100,0);
            }
            mcpText(id,"OK",false,modern);return;
        }
        if("self.camera.take_photo".equals(tool)){
            String question=args.optString("question","Co widzisz?");
            String url=legacyVisionUrl,token=legacyVisionToken;
            if(modern&&meta!=null){
                JSONObject capabilities=meta.optJSONObject("io.modelcontextprotocol/clientCapabilities");
                JSONObject extensions=capabilities==null?null:capabilities.optJSONObject("extensions");
                JSONObject extension=extensions==null?null:extensions.optJSONObject("me.xiaozhi/vision");
                url=extension==null?"":extension.optString("url","");
                token=extension==null?"":extension.optString("token","");
            }
            vision.analyze(question,url,token,(description,error)->{
                try{mcpText(id,error!=null?error:description,error!=null,modern);}
                catch(JSONException ignored){}
            });
            return;
        }
        mcpError(id,-32602,"Nieznane narzędzie");
    }
    private JSONObject serverInfo() throws JSONException{
        return new JSONObject().put("name","Garett Espen").put("version","0.1.0");
    }
    private void mcpResult(Object id,JSONObject value,boolean modern) throws JSONException{
        if(modern){
            value.put("resultType","complete").put("_meta",
                new JSONObject().put("io.modelcontextprotocol/serverInfo",serverInfo()));
        }
        send(new JSONObject().put("type","mcp").put("payload",
            new JSONObject().put("jsonrpc","2.0").put("id",id).put("result",value)));
    }
    private void mcpText(Object id,String message,boolean error,boolean modern) throws JSONException{
        JSONObject result=new JSONObject().put("isError",error)
            .put("content",new JSONArray().put(new JSONObject().put("type","text")
                .put("text",message==null?"":message)));
        mcpResult(id,result,modern);
    }
    private void mcpError(Object id,int code,String message) throws JSONException{
        send(new JSONObject().put("type","mcp").put("payload",
            new JSONObject().put("jsonrpc","2.0").put("id",id).put("error",
                new JSONObject().put("code",code).put("message",message))));
    }
    public void shutdown(){
        closed=true;recording=false;
        WebSocket s=socket;socket=null;if(s!=null)s.cancel();
        sessionReady=false;vision.shutdown();worker.shutdownNow();
        closeAudio();http.dispatcher().executorService().shutdown();
    }
}
