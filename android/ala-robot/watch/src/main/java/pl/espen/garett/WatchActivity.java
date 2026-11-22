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
    private static final int PERM_MIC=10, PERM_CAMERA=11;
    private final Handler main=new Handler(Looper.getMainLooper());
    private FaceView face;
    private XiaozhiClient xiaozhi;
    private AlaGameSession games;
    private TextToSpeech localVoice;
    private boolean voiceReady=false, waitingForIdeaPermission=false;
    private long lastLevelAt;
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
    @Override public void onMicrophone(){
        if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},PERM_MIC);
            return;
        }
        xiaozhi.toggle();
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
                    if(id.startsWith("ala-option-")){
                        try{
                            final int index=Integer.parseInt(id.substring(11));
                            ui(()->face.setHighlightedChoice(index));
                        }catch(NumberFormatException ignored){}
                    }
                }
                @Override public void onDone(String id){
                    if(id.startsWith("ala-option-"))ui(()->face.setHighlightedChoice(-1));
                }
                @Override public void onError(String id){
                    ui(()->face.setHighlightedChoice(-1));
                }
            });
        });
    }
    @Override public void onGames(){
        if(games.isRunning())games.close(); else games.open();
    }
    @Override public void onChoice(int index){games.choose(index);}
    @Override public void onCamera(){
        if(checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){
            requestPermissions(new String[]{Manifest.permission.CAMERA},PERM_CAMERA);
            return;
        }
        xiaozhi.previewCamera();
    }
    @Override public void onSettings(){
        ScrollView scroll=new ScrollView(this);
        LinearLayout form=new LinearLayout(this);form.setPadding(22,12,22,8);
        form.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(form);
        TextView info=new TextView(this);
        info.setText("Ala Robot 0.3.0-beta1 / ESP32-P4 0.2.28\n" +
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
            xiaozhi.testSpeaker();
            Toast.makeText(this,"Odtwarzam 660 Hz. Sprawdź głośność multimediów.",Toast.LENGTH_LONG).show();
        });
        form.addView(speakerTest);
        Button microphoneTest=new Button(this);
        microphoneTest.setText("🎤 Test mikrofonu i Opus (offline)");
        microphoneTest.setOnClickListener(v->{
            if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},PERM_MIC);
                return;
            }
            xiaozhi.testMicrophone();
        });
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
        if(result.length==0||result[0]!=PackageManager.PERMISSION_GRANTED){
            status("Nie udzielono wymaganej zgody");return;
        }
        if(req==PERM_MIC){
            if(waitingForIdeaPermission){
                waitingForIdeaPermission=false;
                xiaozhi.captureIdea();
            } else xiaozhi.toggle();
        }
        if(req==PERM_CAMERA)xiaozhi.previewCamera();
    }
    private void ui(Runnable r){if(!isFinishing())main.post(()->{if(!isFinishing())r.run();});}
    @Override public void state(FaceView.State s){ui(()->{
        face.setState(s);
        if(s==FaceView.State.ERROR && games!=null)games.onNarrationFailed();
        if(s==FaceView.State.LISTENING||s==FaceView.State.CONNECTING||s==FaceView.State.SPEAKING||s==FaceView.State.THINKING)
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    });}
    @Override public void status(String s){ui(()->face.setStatus(s));}
    @Override public void caption(String s){ui(()->face.setSubtitle(s));}
    @Override public void emotion(String e){ui(()->face.setEmotion(e));}
    @Override public void activation(String c){ui(()->face.setActivationCode(c));}
    @Override public void level(int value,boolean microphone){
        long now=android.os.SystemClock.uptimeMillis();
        if(now-lastLevelAt<90)return;
        lastLevelAt=now;
        ui(()->{if(microphone)face.setMicLevel(value);else face.setSpeakerLevel(value);});
    }
    @Override public void ttsSentence(String sentence){
        ui(()->games.onStorySentence(sentence));
    }
    @Override public void userIdea(String sentence){
        ui(()->games.onVoiceIdea(sentence));
    }
    @Override public void turnFinished(){
        ui(()->games.onTurnFinished());
    }
    @Override public void options(String title,String[] choices){
        face.setChoices(title,choices);
    }
    @Override public void sayLocally(String message,String[] choices){
        if(localVoice==null||!voiceReady){
            face.setHighlightedChoice(-1);
            return; // Still fully usable by touch without any TTS engine installed.
        }
        localVoice.stop();
        localVoice.speak(message,TextToSpeech.QUEUE_FLUSH,null,"ala-intro");
        if(choices!=null)for(int i=0;i<choices.length;i++)
            localVoice.speak(choices[i],TextToSpeech.QUEUE_ADD,null,"ala-option-"+i);
    }
    @Override public boolean sayWithAla(String prompt){
        if(localVoice!=null)localVoice.stop();
        return xiaozhi.speakPrompt(prompt);
    }
    @Override public void listenForIdea(){
        if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
            waitingForIdeaPermission=true;
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},PERM_MIC);
        }else xiaozhi.captureIdea();
    }
    @Override public void stopSpeech(){
        if(localVoice!=null)localVoice.stop();
        xiaozhi.stop();
    }
    @Override protected void onPause(){
        super.onPause();
        if(localVoice!=null)localVoice.stop();
        if(xiaozhi!=null)xiaozhi.stop(); // never record in background
    }
    @Override protected void onDestroy(){
        if(localVoice!=null){localVoice.stop();localVoice.shutdown();}
        if(xiaozhi!=null)xiaozhi.shutdown();
        super.onDestroy();
    }
}
