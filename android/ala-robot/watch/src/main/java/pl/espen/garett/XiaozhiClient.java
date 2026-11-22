package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.SystemClock;
import io.github.jaredmdobson.concentus.OpusApplication;
import io.github.jaredmdobson.concentus.OpusEncoder;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.RejectedExecutionException;
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

/** Xiaozhi OTA + WebSocket + raw Opus; all network I/O on worker threads. */
public final class XiaozhiClient {
    public interface Events {
        void state(AlaBehavior.Phase state);
        void status(String text);
        default void provisioning(String text){status(text);}
        void caption(String text);
        void emotion(String emotion);
        void level(int value, boolean microphone);
        void activation(String code);
        void userIdea(String sentence);
        void turnFinished();
        void playedSentence(String sentence);
        default void receivedSentence(String sentence){}
        default void shutter(){}

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
    private final AtomicBoolean diagnosticAudioBusy=new AtomicBoolean();
    private volatile Thread diagnosticSpeaker;
    private final OkHttpClient http;
    private volatile WebSocket socket;
    private volatile boolean recording, closed, sessionStarting, sessionReady, talking, audioActive;
    private volatile Thread recordThread;
    private volatile AudioRecord activeRecord;
    private volatile long lastAudioAt;
    private volatile long micSentFrames;
    private volatile int peakMicRms, sttResponses, ttsResponses;
    private volatile String micStage="idle";
    private volatile String sessionId="";
    private String buttonClip, storyContext, sentence="";
    private boolean contextRead, cameraAnswered, ttsStarted;
    private boolean cameraCommand, cameraAnalyzing, cameraToolRequested;
    private String preparedDescription;
    private JSONObject earlyMcp;
    private volatile boolean cameraCapture, cameraTurn;
    private final java.util.concurrent.atomic.AtomicLong turn=new java.util.concurrent.atomic.AtomicLong();
    private volatile boolean foreground=true;
    private volatile boolean cancelled;
    private ScheduledFuture<?> deadline;
    private long ttsStoppedAt;
    private long receivedBinary;
    private List<byte[]> buttonFrames;
    private volatile boolean captureOnlyIdea=false;
    private String wsUrl, wsToken, legacyVisionUrl, legacyVisionToken;
    private int wsVersion=1, outRate=24000;
    private final Identity identity;

    public XiaozhiClient(Activity activity,Events events){
        this(activity,events,new OkHttpClient.Builder().connectTimeout(10,TimeUnit.SECONDS)
            .readTimeout(15,TimeUnit.SECONDS).writeTimeout(15,TimeUnit.SECONDS)
            .pingInterval(20,TimeUnit.SECONDS).build());
    }

    XiaozhiClient(Activity a,Events listener,OkHttpClient transport){
        http=transport;
        activity=a;events=listener;identity=id(a);vision=new Vision(a,http);
        vision.setEnabled(prefs(a).getBoolean("vision",false)); // opt-in, never silently enable camera upload
        audio=new WatchAudio(a);
    }
    private static SharedPreferences prefs(Context c){return c.getSharedPreferences("espen_watch_v1",Context.MODE_PRIVATE);}
    public static synchronized Identity id(Context c){
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
        stop();
        dispatch(()->{
            wsUrl=null;
            wsToken=null;
            refreshConfig();
        });
    }
    public void setVisionEnabled(boolean enable){
        prefs(activity).edit().putBoolean("vision",enable).apply();vision.setEnabled(enable);
    }
    public boolean isVisionEnabled(){return vision.isEnabled();}
    public void testSpeaker(){
        if (!available() || !diagnosticAudioBusy.compareAndSet(false,true)) {
            events.status("Zakończ rozmowę przed testem dźwięku");
            return;
        }
        diagnosticSpeaker=WatchAudio.testSpeaker(activity,new WatchAudio.Reporter(){
            @Override public void report(String message){if (foreground) events.status(message);}
            @Override public void pcmLevel(int value){}
        },()->diagnosticAudioBusy.set(false));
    }
    public void testMicrophone(){
        if(!available()){
            events.status("Najpierw zakończ rozmowę z Xiaozhi");return;
        }
        if(activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            events.status("Nadaj Ali uprawnienie do mikrofonu");return;
        }
        if (!diagnosticAudioBusy.compareAndSet(false,true)) return;
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
                synchronized (this) {
                    if (!foreground || closed) return;
                    input.startRecording();
                    activeRecord=input;
                }
                if(input.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)
                    throw new IOException("AudioRecord nie nagrywa");
                stage="microphone.read";
                short[] pcm=new short[samples];
                int reads=0,maxRms=0;
                for(int pass=0;pass<16;pass++){
                    int offset=0;
                    while(offset<samples){
                        if (!foreground || closed) return;
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
            }catch(Exception error){
                if (!foreground || closed) return;
                CrashNotes.record(activity,"MIC TEST / "+stage,error);
                result="Test nieudany na etapie "+stage+" ("+
                    error.getClass().getSimpleName()+"). Kopiuj diagnostykę.";
            }finally{
                synchronized (this) {
                    if (activeRecord==input) activeRecord=null;
                    if (input!=null) {
                        try { input.stop(); } catch (RuntimeException ignored) {}
                        input.release();
                    }
                    diagnosticAudioBusy.set(false);
                }
            }
            final String feedback=result;
            events.status(feedback);
            activity.runOnUiThread(()->{
                if (foreground && !closed && !activity.isFinishing()) {
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
        if (!vision.isEnabled()) {
            events.status("Włącz analizę zdjęć w ustawieniach");
            return;
        }
        if (!available()) {
            events.status("Najpierw zakończ obecną rozmowę");
            return;
        }
        long owner=turn.incrementAndGet();
        cameraCapture=true;
        cameraTurn=true;
        events.state(AlaBehavior.Phase.CAPTURING);
        events.status("Robię zdjęcie…");
        vision.preview((message,error)->activity.runOnUiThread(()->{
            if (!foreground || closed || owner!=turn.get() || !cameraCapture) return;
            cameraCapture=false;
            if (error!=null || !vision.hasStagedPhoto()) {
                cameraTurn=false;
                events.status(error==null?"Zdjęcie straciło ważność. Spróbuj ponownie.":error);
                events.state(AlaBehavior.Phase.ERROR);
                return;
            }
            startTurn("camera","",false);
            events.shutter();
        }));
    }

    private boolean available(){
        return foreground && !closed && !busy() && !diagnosticAudioBusy.get();
    }

    long turnId(){return turn.get();}
    boolean cameraActive(){return cameraTurn;}

    /** Sends the same recorded Polish commands as the P4 without opening the microphone. */
    public boolean playButton(String clip,String context){
        if (!available()) return false;
        startTurn(clip,context,false);
        return true;
    }

    public boolean captureIdea(){
        if (!available() || activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
            !=PackageManager.PERMISSION_GRANTED) return false;
        startTurn(null,"",true);
        return true;
    }

    private void startTurn(String clip,String context,boolean idea){
        turn.incrementAndGet();
        cancelled=false;
        sessionStarting=true;
        cameraTurn="camera".equals(clip);
        events.state(cameraTurn ? AlaBehavior.Phase.VISION_ANALYZING : AlaBehavior.Phase.CONNECTING);
        events.status(cameraTurn ? "Łączę z usługą analizy zdjęć…" : "Łączę z Alą…");
        dispatch(()->{
            if (cancelled || !foreground) { finishSession(); return; }
            try {
                if (wsUrl==null) readOta();
                if (cancelled || !foreground) { finishSession(); return; }
                if (wsUrl==null) { fail("Najpierw aktywuj Alę w Xiaozhi"); return; }
                captureOnlyIdea=idea;
                buttonClip=clip;
                storyContext=context;
                contextRead=false;
                cameraAnswered=false;
                cameraCommand=cameraAnalyzing=cameraToolRequested=false;
                preparedDescription=null;
                earlyMcp=null;
                sentence="";
                ttsStarted=false;
                ttsStoppedAt=0;
                receivedBinary=0;
                micSentFrames=0;
                peakMicRms=0;
                sttResponses=ttsResponses=0;
                if (clip!=null) {
                    try (InputStream input=activity.getAssets().open("ala/"+clip+".ogg")) {
                        buttonFrames=ButtonAudio.read(input);
                    }
                }
                openSocket();
                armDeadline(12,"Brak potwierdzenia sesji Xiaozhi");
            } catch (Exception e) {
                fail("Xiaozhi: "+brief(e));
            }
        });
    }

    private void dispatch(Runnable task){
        try { if (!closed) worker.execute(task); }
        catch (RejectedExecutionException ignored) { /* Activity destroyed. */ }
    }

    private void armDeadline(long seconds,String message){
        if (deadline!=null) deadline.cancel(false);
        long owner=turn.get();
        deadline=worker.schedule(()->{if (turn.get()==owner && !cancelled) fail(message);},seconds,TimeUnit.SECONDS);
    }

    public void refreshConfig(){
        if(closed||!otaBusy.compareAndSet(false,true))return;
        worker.execute(()->{
            try{readOta();}
            catch(Exception e){events.provisioning("Xiaozhi OTA: "+brief(e));}
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
            events.provisioning("Dodaj to urządzenie do agenta Ala w Xiaozhi");
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
        if(version<1 || version>3)throw new IOException("Nieobsługiwana wersja protokołu: "+version);
        wsUrl=endpoint;wsToken=ws.optString("token","");wsVersion=version;
        events.activation("");events.provisioning("Gotowa. Dotknij mikrofonu!");
    }
    private void checkActivation(String ota){
        try{
            String activationUrl=ota.endsWith("/")?ota+"activate":ota+"/activate";
            Request request=new Request.Builder().url(activationUrl)
                .header("Device-Id",identity.deviceId).header("Client-Id",identity.clientId)
                .header("Activation-Version","1")
                .post(RequestBody.create(JSON,"{}")).build();
            try(Response response=http.newCall(request).execute()){
                if(response.code()==200)events.provisioning("Sparowano! Odświeżam Xiaozhi…");
            }
        }catch(Exception ignored){ /* OTA poll will retry; no credentials in logs. */ }
    }
    public void toggle(){
        if (recording) {
            dispatch(()->finishMicrophone(socket));
        } else if (sessionStarting || socket!=null) {
            stop();
        } else if (available()) {
            startMicrophone(false);
        }
    }

    private void startMicrophone(boolean idea){
        if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED) {
            events.status("Zezwól aplikacji na mikrofon");
            return;
        }
        startTurn(null,"",idea);
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
                dispatch(()->{
                    if (webSocket!=socket || cancelled) return;
                    try {
                        JSONObject hello=new JSONObject().put("type","hello").put("version",wsVersion)
                            .put("transport","websocket").put("features",new JSONObject().put("mcp",true).put("aec",false))
                            .put("audio_params",new JSONObject().put("format","opus")
                                .put("sample_rate",16000).put("channels",1).put("frame_duration",60));
                        if (!webSocket.send(hello.toString())) fail("Nie udało się otworzyć sesji Xiaozhi");
                    } catch (JSONException e) { fail("Hello: "+brief(e)); }
                });
            }
            @Override public void onMessage(WebSocket webSocket,String text){
                dispatch(()->parseMessage(webSocket,text));
            }
            @Override public void onMessage(WebSocket webSocket,ByteString bytes){
                dispatch(()->{
                    if (webSocket!=socket || !sessionReady || cancelled || !talking || !acceptOutput()) return;
                    if (bytes.size()>8192 || ++receivedBinary>1600) {
                        fail("Xiaozhi wysłało audio poza limitem sesji");
                        return;
                    }
                    try {
                        ensureAudio();
                        audio.offer(WsAudio.decode(bytes,wsVersion),sentence);
                    } catch (IOException error) { fail("Audio Xiaozhi: "+brief(error)); }
                });
            }
            @Override public void onFailure(WebSocket webSocket,Throwable t,Response response){
                dispatch(()->{ if (webSocket==socket) fail("Xiaozhi: "+brief(t)); });
            }
            @Override public void onClosing(WebSocket webSocket,int code,String reason){
                dispatch(()->{
                    if (webSocket==socket && ttsStoppedAt==0) fail("Xiaozhi zamknęło sesję przed końcem odpowiedzi");
                });
            }
            @Override public void onClosed(WebSocket webSocket,int code,String reason){
                dispatch(()->{
                    if (webSocket==socket && ttsStoppedAt==0) fail("Xiaozhi zamknęło sesję");
                });
            }
        });
    }

    private boolean acceptOutput(){
        return !captureOnlyIdea && (!"story_context".equals(buttonClip) || contextRead)
            && (!"camera".equals(buttonClip) || cameraAnswered);
    }

    private void pumpButton(WebSocket sender,int index){
        if (sender!=socket || cancelled || !foreground || buttonFrames==null) return;
        if (index==buttonFrames.size()) {
            buttonFrames=null;
            sendListen("stop");
            if (cameraTurn && !cameraAnswered && !cameraToolRequested)
                armDeadline(22,"Xiaozhi nie wywołało narzędzia aparatu. Sprawdź obsługę Vision w agencie.");
            return;
        }
        if (!sender.send(WsAudio.encode(buttonFrames.get(index),wsVersion,SystemClock.elapsedRealtime()))) {
            fail("Nie udało się wysłać polecenia audio");
            return;
        }
        worker.schedule(()->pumpButton(sender,index+1),60,TimeUnit.MILLISECONDS);
    }

    private void sendListen(String state){
        try {
            JSONObject message=new JSONObject().put("type","listen").put("state",state);
            if ("start".equals(state)) message.put("mode","manual");
            send(message);
        } catch (JSONException error) { fail(brief(error)); }
    }

    private void parseMessage(WebSocket sender,String text){
        if (sender!=socket || closed || cancelled) return;
        try {
            JSONObject message=new JSONObject(text);
            String type=message.optString("type");
            String wireSession=message.optString("session_id","");
            if (sessionReady && !"hello".equals(type) && !wireSession.isEmpty()
                && !wireSession.equals(sessionId)) return;
            switch (type) {
                case "hello":
                    if (sessionReady || !"websocket".equals(message.optString("transport")))
                        throw new IOException("Zły transport lub ponowne hello");
                    sessionId=message.optString("session_id","");
                    if (sessionId.isEmpty()) throw new IOException("Brak identyfikatora sesji");
                    JSONObject params=message.optJSONObject("audio_params");
                    outRate=params==null ? 24000 : params.optInt("sample_rate",24000);
                    if (outRate!=16000 && outRate!=24000 && outRate!=48000)
                        throw new IOException("Nieobsługiwana częstotliwość TTS");
                    audio.resetSession();
                    sessionStarting=false;
                    sessionReady=true;
                    talking=false;
                    if (earlyMcp!=null) {
                        handleMcp(earlyMcp);
                        earlyMcp=null;
                    }
                    if (cameraTurn) {
                        armDeadline(35,"Xiaozhi nie udostępniło opisu zdjęcia.");
                        events.state(AlaBehavior.Phase.VISION_ANALYZING);
                        events.status("Analizuję zdjęcie…");
                        prepareCamera(sender);
                        worker.schedule(()->{
                            if (sender==socket && !cancelled && !cameraAnalyzing)
                                startCameraCommand(sender);
                        },1200,TimeUnit.MILLISECONDS);
                    } else {
                        armDeadline(buttonClip==null ? 32 : 65,"Xiaozhi nie zakończyło nagrywania lub odpowiedzi");
                        sendListen("start");
                        if (sender!=socket) return;
                        if (buttonClip!=null) {
                            events.state(AlaBehavior.Phase.THINKING);
                            events.status("Ala przygotowuje opowieść…");
                            pumpButton(sender,0);
                        } else {
                            events.state(AlaBehavior.Phase.LISTENING);
                            events.status("Słucham…");
                            beginMic(sender);
                        }
                    }
                    break;
                case "stt":
                    if (!sessionReady || buttonClip!=null) break;
                    sttResponses++;
                    String phrase=message.optString("text","").trim();
                    if (captureOnlyIdea) {
                        finishSession();
                        events.userIdea(phrase);
                    } else {
                        vision.noteUserSpeech(phrase);
                        events.caption(phrase);
                        if (!talking) events.state(AlaBehavior.Phase.THINKING);
                    }
                    break;
                case "llm":
                    if (sessionReady && acceptOutput()) events.emotion(message.optString("emotion","neutral"));
                    break;
                case "tts":
                    if (captureOnlyIdea) {
                        fail("Nie rozpoznałam pomysłu. Spróbuj jeszcze raz.");
                        break;
                    }
                    if (!sessionReady) break;
                    String state=message.optString("state","");
                    if ("start".equals(state)) {
                        ttsResponses++;
                        ttsStarted=true;
                        talking=true;
                        ttsStoppedAt=0;
                        sentence="";
                        endMic();
                        if (!cameraTurn || cameraAnswered)
                            armDeadline(65,"Xiaozhi nie zakończyło wypowiedzi");
                    } else if ("sentence_start".equals(state) && talking && acceptOutput()) {
                        sentence=message.optString("text","");
                        if (!sentence.trim().isEmpty()) events.receivedSentence(sentence);
                    } else if ("stop".equals(state) && ttsStarted && talking) {
                        talking=false;
                        if (!acceptOutput()) break;
                        ttsStoppedAt=SystemClock.elapsedRealtime();
                        audio.seal();
                        worker.schedule(()->finishAfterAudio(sender),250,TimeUnit.MILLISECONDS);
                    }
                    break;
                case "mcp":
                    JSONObject request=message.optJSONObject("payload");
                    if (sessionReady && !captureOnlyIdea) handleMcp(request);
                    else if (request!=null && "initialize".equals(request.optString("method"))) earlyMcp=request;
                    break;
                case "alert":
                    events.status(message.optString("message","Informacja od Xiaozhi"));
                    break;
                default: break;
            }
        } catch (Exception error) { fail("Protokół: "+brief(error)); }
    }

    private void send(JSONObject body){
        try{
            if(!sessionId.isEmpty())body.put("session_id",sessionId);
            WebSocket s=socket;if(s!=null && !cancelled && !s.send(body.toString())) fail("Połączenie Xiaozhi zostało zamknięte");
        }catch(JSONException ignored){}
    }
    private void beginMic(WebSocket owner){
        if(recording)return;
        recording=true;
        recordThread=new Thread(()->recordLoop(owner),"espen-mic");
        recordThread.setDaemon(true);recordThread.start();
    }
    private void recordLoop(WebSocket owner){
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
            synchronized (this) {
                if (!recording || cancelled || !foreground || owner!=socket) return;
                record.startRecording();
                activeRecord=record;
            }
            if(record.getRecordingState()!=AudioRecord.RECORDSTATE_RECORDING)
                throw new IOException("Android nie rozpoczął nagrywania");
            SpeechEndpoint endpoint=new SpeechEndpoint(SystemClock.elapsedRealtime());
            while(recording && owner==socket && !cancelled && foreground && !talking){
                micStage="audio.read";
                int off=0;
                while(off<samples && recording){
                    int count=record.read(pcm,off,samples-off);
                    if(count<=0)throw new IOException("AudioRecord.read: "+count);
                    off+=count;
                }
                if(!recording || cancelled || owner!=socket)break;
                long energy=0;for(short v:pcm){energy+=(long)v*v;}
                int rms=(int)Math.sqrt(energy/(double)samples);
                if(rms>peakMicRms)peakMicRms=rms;
                events.level(Math.min(100,rms/55),true);
                long now=SystemClock.elapsedRealtime();
                SpeechEndpoint.Result result=endpoint.frame(rms,now);
                micStage="opus.encode";
                int length=encoder.encode(pcm,0,samples,opus,0,opus.length);
                micStage="websocket.send";
                WebSocket s=socket;
                if (recording && !cancelled && foreground && s==owner && length>0) {
                    if (s.queueSize()>250_000 || !s.send(WsAudio.encode(
                            java.util.Arrays.copyOf(opus,length),wsVersion,now)))
                        throw new IOException("Przerwano wysyłanie nagrania");
                    micSentFrames++;
                }
                if (result==SpeechEndpoint.Result.NO_SPEECH) {
                    dispatch(()->{
                        if (owner==socket && recording && !cancelled)
                            fail("Nie usłyszałam wypowiedzi. Dotknij mikrofonu i spróbuj ponownie.");
                    });
                    return;
                }
                if (result!=SpeechEndpoint.Result.CONTINUE) break;
            }
            dispatch(()->finishMicrophone(owner));
        } catch (Exception error) {
            if (recording && !cancelled && owner==socket) {
                CrashNotes.record(activity,"MIC / "+micStage,error);
                dispatch(()->{ if (owner==socket && recording) fail("Mikrofon: "+brief(error)); });
            }
        } finally {
            synchronized (this) {
                if (activeRecord==record) activeRecord=null;
                if (record!=null) {
                    try { record.stop(); } catch (RuntimeException ignored) {}
                    record.release();
                }
            }
        }
    }

    private synchronized void endMic(){
        recording=false;
        if (activeRecord!=null) {
            try { activeRecord.stop(); } catch (RuntimeException ignored) {}
        }
    }

    private void finishMicrophone(WebSocket owner){
        if (owner==null || owner!=socket || !recording || talking || cancelled) return;
        endMic();
        micStage="finish";
        events.level(0,true);
        events.state(AlaBehavior.Phase.THINKING);
        events.status("Ala myśli…");
        armDeadline(30,"Xiaozhi nie odpowiedziało na wypowiedź");
        sendListen("stop");
    }

    private void ensureAudio(){
        if (audioActive) return;
        audioActive=true;
        long owner=turn.get();
        audio.start(outRate,recordThread,"story_context".equals(buttonClip) ? 12 : 1,new WatchAudio.Reporter(){
            private boolean current(){return turn.get()==owner && !cancelled && !closed && audioActive;}
            @Override public void report(String message){if (current()) events.status(message);}
            @Override public void pcmLevel(int value){if (current()) events.level(value,false);}
            @Override public void started(){if (current()) events.state(AlaBehavior.Phase.SPEAKING);}
            @Override public void sentence(String value){if (current()) events.playedSentence(value);}
        });
    }

    private void finishAfterAudio(WebSocket owner){
        if (socket!=owner || cancelled || ttsStoppedAt==0 || talking) return;
        if (!audio.drained() && SystemClock.elapsedRealtime()-ttsStoppedAt<16_000) {
            worker.schedule(()->finishAfterAudio(owner),150,TimeUnit.MILLISECONDS);
            return;
        }
        if (!audio.drained() || !audio.complete()) {
            fail("Xiaozhi zakończyło niepełną odpowiedź audio");
            return;
        }
        finishSession();
        events.turnFinished();
    }

    private void finishSession(){
        if (deadline!=null) deadline.cancel(false);
        WebSocket previous=socket;
        socket=null;
        sessionReady=false;
        talking=false;
        ttsStoppedAt=0;
        endMic();
        buttonFrames=null;
        buttonClip=null;
        storyContext=null;
        captureOnlyIdea=false;
        if (previous!=null) previous.cancel();
        audioActive=false;
        audio.stop();
        vision.cancel();
        sessionStarting=false;
        cameraCapture=false;
        cameraTurn=false;
        preparedDescription=null;
        if (!closed) {
            events.state(AlaBehavior.Phase.IDLE);
            events.status("Dotknij mikrofonu, by porozmawiać");
        }
    }

    public boolean busy(){ return cameraCapture || sessionStarting || socket!=null; }

    public void foreground(boolean active){
        foreground=active;
        if (!active) stop();
    }

    public void stop(){
        boolean active=(sessionStarting || socket!=null) && !cancelled;
        boolean capturing=cameraCapture;
        cameraCapture=false;
        if (capturing || active || !cancelled) turn.incrementAndGet();
        cancelled=true;
        if (!active) cameraTurn=false;
        endMic();
        if (diagnosticSpeaker!=null) diagnosticSpeaker.interrupt();
        audio.stop();
        vision.cancel();
        if (active) dispatch(()->{
            if (talking && socket!=null) {
                try {
                    socket.send(new JSONObject().put("type","abort").put("session_id",sessionId).toString());
                } catch (JSONException ignored) {}
            }
            finishSession();
        });
        else if (capturing && foreground && !closed) events.state(AlaBehavior.Phase.IDLE);
    }

    void afterStop(Runnable action){
        stop();
        dispatch(()->activity.runOnUiThread(()->{
            if (foreground && !closed) action.run();
        }));
    }

    private void fail(String why){
        long owner=turn.get();
        boolean active=!cancelled;
        finishSession();
        if (active && owner==turn.get() && !cancelled) {
            events.status(why);
            events.state(AlaBehavior.Phase.ERROR);
        }
    }

    private void prepareCamera(WebSocket owner){
        if (!cameraTurn || !sessionReady || cameraCommand || cameraAnalyzing
            || legacyVisionUrl==null || legacyVisionUrl.isEmpty()) return;
        cameraAnalyzing=true;
        vision.analyze("Opisz dziecku po polsku, co widać na tym zdjęciu.",
            legacyVisionUrl,legacyVisionToken,(description,error)->dispatch(()->{
                if (owner!=socket || cancelled) return;
                if (error!=null) { fail(error); return; }
                preparedDescription=description;
                cameraAnalyzing=false;
                startCameraCommand(owner);
            }));
    }

    private void startCameraCommand(WebSocket owner){
        if (owner!=socket || cancelled || cameraCommand || !cameraTurn) return;
        cameraCommand=true;
        sendListen("start");
        if (owner==socket) pumpButton(owner,0);
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
            if (cameraTurn && socket!=null) prepareCamera(socket);
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
            JSONArray offered=new JSONArray().put(camera).put(volume);
            offered.put(new JSONObject().put("name","self.story.read_context")
                .put("description","Kontekst Magicznej Opowieści. Odczytaj go przed dalszą narracją po polsku.")
                .put("inputSchema",new JSONObject().put("type","object").put("properties",new JSONObject())));
            mcpResult(id,new JSONObject().put("tools",offered),modern);
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
        if ("self.story.read_context".equals(tool)) {
            if (!"story_context".equals(buttonClip) || storyContext==null || storyContext.isEmpty()) {
                mcpText(id,"Brak aktywnego rozdziału",true,modern);
            } else {
                contextRead=true;
                mcpText(id,storyContext,false,modern);
            }
            return;
        }
        if("self.camera.take_photo".equals(tool)){
            if (cameraTurn) {
                if (!cameraCommand || cameraToolRequested) {
                    mcpText(id,"Brak nowego zdjęcia do odczytania",true,modern);
                    return;
                }
                cameraToolRequested=true;
                armDeadline(28,"Xiaozhi nie wypowiedziało opisu zdjęcia.");
                if (preparedDescription!=null) {
                    cameraAnswered=true;
                    mcpText(id,preparedDescription,false,modern);
                    preparedDescription=null;
                    return;
                }
            }
            String question=args.optString("question","Co widzisz?");
            String url=legacyVisionUrl,token=legacyVisionToken;
            if(modern&&meta!=null){
                JSONObject capabilities=meta.optJSONObject("io.modelcontextprotocol/clientCapabilities");
                JSONObject extensions=capabilities==null?null:capabilities.optJSONObject("extensions");
                JSONObject extension=extensions==null?null:extensions.optJSONObject("me.xiaozhi/vision");
                if (extension!=null && !extension.optString("url","").isEmpty()) {
                    url=extension.optString("url","");
                    token=extension.optString("token","");
                }
            }
            WebSocket owner=socket;
            vision.analyze(question,url,token,(description,error)->dispatch(()->{
                if (owner!=socket || cancelled) return;
                try {
                    if ("camera".equals(buttonClip) && error!=null) { fail(error); return; }
                    cameraAnswered=error==null && description!=null && !description.trim().isEmpty();
                    mcpText(id,error!=null?error:description,error!=null,modern);
                } catch (JSONException problem) { fail(brief(problem)); }
            }));
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
        closed=true;
        turn.incrementAndGet();
        foreground=false;
        cancelled=true;
        endMic();
        if (diagnosticSpeaker!=null) diagnosticSpeaker.interrupt();
        WebSocket previous=socket;
        socket=null;
        if (previous!=null) previous.cancel();
        sessionReady=false;
        vision.shutdown();
        worker.shutdownNow();
        audio.stop();
        http.dispatcher().cancelAll();
        http.dispatcher().executorService().shutdown();
    }
}
