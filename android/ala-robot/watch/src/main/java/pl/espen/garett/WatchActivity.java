package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowManager;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import java.util.Locale;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** Foreground-only watch UX. No accessibility, SMS, SOS or background service access. */
public final class WatchActivity extends Activity implements FaceView.Actions, XiaozhiClient.Events, AlaGameSession.Ui {
    private static final int PERM_MIC=10, PERM_CAMERA=11, PERM_IDEA=12, PERM_MIC_TEST=13;
    private final Handler main=new Handler(Looper.getMainLooper());
    private FaceView face;
    private XiaozhiClient xiaozhi;
    private AlaGameSession games;
    private TextToSpeech localVoice;
    private volatile boolean voiceReady;
    private boolean resumed,localSpeaking,listenAfterLocalSpeech;
    private int pendingPermission, grantedAction, utterance;
    private String utterancePrefix="";
    private String lastUtterance="";
    private volatile int uiEpoch;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        CrashNotes.install(this);
        getWindow().setStatusBarColor(0xff000000);
        getWindow().setNavigationBarColor(0xff000000);
        immersive();
        face=new FaceView(this);face.setActions(this);
        setContentView(face);
        xiaozhi=new XiaozhiClient(this,this);
        games=new AlaGameSession(this);
        initLocalSpeech();
        xiaozhi.refreshConfig();
    }
    private void immersive(){
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }
    @Override public void onWindowFocusChanged(boolean focused){
        super.onWindowFocusChanged(focused);
        if (focused) immersive();
    }

    @Override public void onMicrophone(){
        if (games.wantsIdea() && !xiaozhi.busy()) { listenForIdea(); return; }
        if (games.isRunning()) {
            games.close();
            return;
        }
        requestAction(PERM_MIC,Manifest.permission.RECORD_AUDIO);
    }

    private void requestAction(int code,String permission){
        if (!resumed || pendingPermission!=0) return;
        if (checkSelfPermission(permission)==PackageManager.PERMISSION_GRANTED) {
            authorizedAction(code);
        } else {
            pendingPermission=code;
            requestPermissions(new String[]{permission},code);
        }
    }

    private void authorizedAction(int action){
        stopLocalSpeech();
        switch (action) {
            case PERM_MIC: xiaozhi.toggle(); break;
            case PERM_CAMERA: xiaozhi.previewCamera(); break;
            case PERM_IDEA: if (games.wantsIdea()) xiaozhi.captureIdea(); break;
            case PERM_MIC_TEST: xiaozhi.testMicrophone(); break;
            default: break;
        }
    }

    private void initLocalSpeech(){
        localVoice=new TextToSpeech(this,result->{
            if(result!=TextToSpeech.SUCCESS||localVoice==null)return;
            int language=localVoice.setLanguage(Locale.forLanguageTag("pl-PL"));
            voiceReady=language!=TextToSpeech.LANG_NOT_SUPPORTED &&
                language!=TextToSpeech.LANG_MISSING_DATA;
            localVoice.setSpeechRate(0.95f);
            localVoice.setOnUtteranceProgressListener(new UtteranceProgressListener(){
                @Override public void onStart(String id){
                    ui(()->{
                        if (!id.startsWith(utterancePrefix) || !localSpeaking) return;
                        face.setState(AlaBehavior.Phase.SPEAKING);
                        face.setHighlightedChoice(Integer.parseInt(id.substring(utterancePrefix.length())));
                    });
                }
                @Override public void onDone(String id){
                    ui(()->{
                        if (!localSpeaking || !id.startsWith(utterancePrefix)) return;
                        face.setHighlightedChoice(-1);
                        if (id.equals(lastUtterance)) {
                            boolean listen=listenAfterLocalSpeech;
                            finishLocalSpeech();
                            if (listen && games.wantsIdea()) listenForIdea();
                        }
                    });
                }
                @Override public void onError(String id){
                    ui(()->{ if (localSpeaking && id.startsWith(utterancePrefix)) finishLocalSpeech(); });
                }
            });
        });
    }

    @Override public void onGames(){
        if (games.isRunning()) games.close();
        else if (xiaozhi.busy()) xiaozhi.afterStop(()->{if (resumed) games.open();});
        else games.open();
    }
    @Override public void onChoice(int index){games.choose(index);}
    @Override public void onCamera(){
        if (games.isRunning()) games.close();
        if (xiaozhi.busy())
            xiaozhi.afterStop(()->requestAction(PERM_CAMERA,Manifest.permission.CAMERA));
        else requestAction(PERM_CAMERA,Manifest.permission.CAMERA);
    }
    @Override public void onSettings(){
        ScrollView scroll=new ScrollView(this);
        LinearLayout form=new LinearLayout(this);form.setPadding(22,12,22,8);
        form.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(form);
        TextView info=new TextView(this);
        info.setText("Ala Robot 0.3.0-beta2 / ESP32-P4 0.2.28\n" +
            "Telefon i zegarek mają własną aktywację w Xiaozhi.\n" +
            "Mikrofon działa tylko podczas sesji. Gry: ikona z kontrolerem.\n");
        info.setTextSize(14);form.addView(info);
        EditText ota=new EditText(this);ota.setSingleLine(false);ota.setMaxLines(3);
        ota.setText(XiaozhiClient.getOta(this));ota.setTextSize(13);ota.setHint("Adres OTA HTTPS");
        form.addView(ota);
        CheckBox vision=new CheckBox(this);vision.setChecked(xiaozhi.isVisionEnabled());
        vision.setText("Aparat i analiza zdjęć (zgoda użytkownika, domyślnie WYŁ.)");
        vision.setTextSize(13);form.addView(vision);
        TextView privacy=new TextView(this);
        privacy.setText("Zdjęcie tylko na jawne pytanie lub dotknięcie ikony aparatu. " +
            "Do analizy serwer musi obsługiwać Xiaozhi Vision (MCP/HTTPS).");
        privacy.setTextSize(12);form.addView(privacy);
        TextView ids=new TextView(this);
        XiaozhiClient.Identity id=XiaozhiClient.id(this);
        ids.setText("\nDevice-ID: "+id.deviceId+"\nClient-ID: "+id.clientId);
        ids.setTextSize(10);form.addView(ids);
        Button speakerTest=new Button(this);
        speakerTest.setText("🔊 Test głośnika (660 Hz)");
        speakerTest.setOnClickListener(v->{
            stopLocalSpeech();
            xiaozhi.testSpeaker();
            Toast.makeText(this,"Odtwarzam 660 Hz. Sprawdź głośność multimediów.",Toast.LENGTH_LONG).show();
        });
        form.addView(speakerTest);
        Button microphoneTest=new Button(this);
        microphoneTest.setText("🎤 Test mikrofonu i Opus (offline)");
        microphoneTest.setOnClickListener(v->requestAction(PERM_MIC_TEST,Manifest.permission.RECORD_AUDIO));
        form.addView(microphoneTest);
        Button diagnostic=new Button(this);
        diagnostic.setText("Diagnostyka dźwięku i awarii");
        diagnostic.setOnClickListener(v->{
            String infoText=xiaozhi.audioDiagnostics()+"\n\n"+CrashNotes.read(this);
            new AlertDialog.Builder(this).setTitle("Ala: diagnostyka")
                .setMessage(infoText)
                .setPositiveButton("Kopiuj",(dialog,which)->{
                    ClipboardManager clipboard=(ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
                    if(clipboard!=null)clipboard.setPrimaryClip(ClipData.newPlainText("Ala logs",infoText));
                    Toast.makeText(this,"Skopiowano diagnostykę",Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("Zamknij",null).show();
        });
        form.addView(diagnostic);
        new AlertDialog.Builder(this).setTitle("Ustawienia Ali").setView(scroll)
            .setPositiveButton("Zapisz",(d,w)->{
                xiaozhi.setVisionEnabled(vision.isChecked());
                try{
                    if(!XiaozhiClient.getOta(this).equals(ota.getText().toString().trim()))
                        xiaozhi.setOta(ota.getText().toString().trim());
                    else xiaozhi.refreshConfig();
                }catch(Exception e){status(e.getMessage());}
            })
            .setNegativeButton("Zamknij",null)
            .setNeutralButton("Odśwież kod",(d,w)->xiaozhi.refreshConfig())
            .show();
    }
    @Override public void onRequestPermissionsResult(int req,String[] names,int[] result){
        super.onRequestPermissionsResult(req,names,result);
        if (req!=pendingPermission) return;
        pendingPermission=0;
        if (result.length==0 || result[0]!=PackageManager.PERMISSION_GRANTED) {
            face.setStatus("Nie udzielono wymaganej zgody");
            if (req==PERM_IDEA) games.onNarrationFailed();
        } else if (resumed) {
            authorizedAction(req);
        } else {
            grantedAction=req;
        }
    }

    private void ui(Runnable action){
        int epoch=uiEpoch;
        main.post(()->{
            if (epoch==uiEpoch && resumed && !isFinishing() && !isDestroyed()) action.run();
        });
    }

    @Override public void state(AlaBehavior.Phase phase){ui(()->{
        if (localSpeaking && phase==AlaBehavior.Phase.IDLE) return;
        face.setState(phase);
        if (phase==AlaBehavior.Phase.ERROR) games.onNarrationFailed();
        if (phase==AlaBehavior.Phase.LISTENING || phase==AlaBehavior.Phase.CONNECTING
            || phase==AlaBehavior.Phase.SPEAKING || phase==AlaBehavior.Phase.THINKING)
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    });}
    @Override public void status(String message){ui(()->face.setStatus(message));}
    @Override public void caption(String message){ui(()->{
        face.setSubtitle(message);
        face.userSpeech(message);
    });}
    @Override public void emotion(String emotion){ui(()->face.setEmotion(emotion));}
    @Override public void activation(String code){ui(()->face.setActivationCode(code));}
    @Override public void level(int value,boolean microphone){
        ui(()->{if (microphone) face.setMicLevel(value); else face.setSpeakerLevel(value);});
    }
    @Override public void playedSentence(String sentence){ui(()->{
        face.setSubtitle(sentence);
        games.onPlayedSentence(sentence);
    });}
    @Override public void userIdea(String sentence){ui(()->games.onVoiceIdea(sentence));}
    @Override public void turnFinished(){ui(()->games.onTurnFinished());}
    @Override public void options(String title,String[] choices){
        face.game(games.isRunning());
        face.setChoices(title,choices);
    }
    @Override public void highlight(int index){face.setHighlightedChoice(index);}
    @Override public void reaction(AlaBehavior.Mood mood){face.reaction(mood);}

    @Override public void sayLocally(String message,String[] choices){
        stopLocalSpeech();
        if (xiaozhi.busy()) return;
        if (localVoice==null || !voiceReady) {
            face.setStatus("Polski głos do menu niedostępny");
            return;
        }
        utterancePrefix="ala-"+(++utterance)+":";
        lastUtterance=utterancePrefix+(choices==null ? -1 : choices.length-1);
        localSpeaking=true;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (localVoice.speak(message,TextToSpeech.QUEUE_FLUSH,null,utterancePrefix+"-1")==TextToSpeech.ERROR) {
            finishLocalSpeech();
            return;
        }
        if (choices!=null) for (int i=0;i<choices.length;i++)
            localVoice.speak(choices[i],TextToSpeech.QUEUE_ADD,null,utterancePrefix+i);
    }

    private void finishLocalSpeech(){
        localSpeaking=false;
        listenAfterLocalSpeech=false;
        face.setHighlightedChoice(-1);
        face.setState(AlaBehavior.Phase.IDLE);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private void stopLocalSpeech(){
        if (localVoice!=null) localVoice.stop();
        if (localSpeaking) finishLocalSpeech();
    }

    @Override public boolean playButton(String clip,String context){
        stopLocalSpeech();
        return xiaozhi.playButton(clip,context);
    }
    @Override public void sayIdeaPrompt(String message){
        sayLocally(message,null);
        listenAfterLocalSpeech=localSpeaking;
        if (!listenAfterLocalSpeech) face.setStatus("Dotknij mikrofonu, aby podać swój pomysł");
    }
    @Override public void listenForIdea(){requestAction(PERM_IDEA,Manifest.permission.RECORD_AUDIO);}
    @Override public void stopSpeech(){
        stopLocalSpeech();
        xiaozhi.stop();
    }

    @Override protected void onResume(){
        super.onResume();
        resumed=true;
        xiaozhi.foreground(true);
        face.foreground(true);
        int action=grantedAction;
        grantedAction=0;
        if (action!=0) authorizedAction(action);
    }

    @Override protected void onPause(){
        resumed=false;
        uiEpoch++;
        stopLocalSpeech();
        if (pendingPermission==0 && games.isRunning()) games.close();
        xiaozhi.foreground(false);
        face.foreground(false);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onPause();
    }

    @Override public void onBackPressed(){
        if (games.isRunning()) games.close();
        else if (xiaozhi.busy()) xiaozhi.stop();
        else super.onBackPressed();
    }

    @Override protected void onDestroy(){
        if (localVoice!=null) {
            localVoice.shutdown();
            localVoice=null;
        }
        xiaozhi.shutdown();
        main.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
}
