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
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** Foreground-only watch UX. No accessibility, SMS, SOS or background service access. */
public final class WatchActivity extends Activity implements FaceView.Actions, XiaozhiClient.Events {
    private static final int PERM_MIC=10, PERM_CAMERA=11, PERM_WAKE=12;
    private final Handler main=new Handler(Looper.getMainLooper());
    private FaceView face;
    private XiaozhiClient xiaozhi;
    private WatchWake wake;
    private boolean sleeping;
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
        wake=new WatchWake(this,new WatchWake.Events(){
            @Override public void onWake(){ui(()->{
                if(!sleeping)return;
                // The local recorder must be released before starting cloud mic.
                main.postDelayed(()->onMicrophone(),350);
            });}
            @Override public void message(String text){status(text);}
        });
        face.setBattery(xiaozhi.controls().battery());
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
        if(sleeping){
            sleeping=false;wake.stop();
            face.setState(FaceView.State.IDLE);
            main.postDelayed(()->{if(!isFinishing())xiaozhi.toggle();},400);
        }else if(face.getState()==FaceView.State.SPEAKING){
            xiaozhi.interrupt();
        }else{
            wake.stop();
            xiaozhi.toggle();
        }
    }
    @Override public void onStandby(){
        sleeping=true;
        wake.stop();
        xiaozhi.standby();
        face.setState(FaceView.State.STANDBY);
        face.setStatus("Bez odbioru • dotknij lub powiedz hasło");
        if(wake.trained()==3){
            main.postDelayed(()->{if(sleeping&&!isFinishing())wake.listen();},650);
        }
    }
    @Override public void onControls(){
        wake.stop();
        xiaozhi.controls().showPanel();
    }
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
        info.setText("Ala Watch 0.2.12 (Espen 0.2.11+)\n" +
            "Przesuń w dół: bez odbioru; w górę: sterowanie.\\n" +
            "Podczas mowy Ali dotknij mikrofonu, aby ją przerwać.\\n" +
            "Lokalne hasło wymaga 3 próbek i działa tylko w otwartej aplikacji.\\n");
        info.setTextSize(14);form.addView(info);
        EditText ota=new EditText(this);ota.setSingleLine(false);ota.setMaxLines(3);
        ota.setText(XiaozhiClient.getOta(this));ota.setTextSize(13);ota.setHint("Adres OTA HTTPS");
        form.addView(ota);
        CheckBox vision=new CheckBox(this);vision.setChecked(xiaozhi.isVisionEnabled());
        vision.setText("Aparat i analiza zdjęć (domyślnie WŁ.)");
        vision.setTextSize(13);form.addView(vision);
        CheckBox interruption=new CheckBox(this);
        interruption.setChecked(xiaozhi.bargeIn());
        interruption.setText("Przerywanie głosem podczas TTS (eksperymentalne)");
        interruption.setTextSize(12);form.addView(interruption);
        TextView disclaimer=new TextView(this);
        disclaimer.setText("Na niektórych Garettach jednoczesny mikrofon i głośnik jest zablokowany. Dotyk zawsze przerywa rozmowę.");
        disclaimer.setTextSize(11);form.addView(disclaimer);
        Button panel=new Button(this);panel.setText("⌚ Sterowanie zegarkiem");
        panel.setOnClickListener(v->onControls());form.addView(panel);
        Button teach=new Button(this);
        teach.setText("🎤 Naucz hasła lokalnie ("+wake.trained()+"/3)");
        teach.setOnClickListener(v->{
            if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){
                requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},PERM_WAKE);return;
            }
            xiaozhi.stop();
            main.postDelayed(()->{wake.train();teach.setText("🎤 Naucz hasła ("+wake.trained()+"/3)");},400);
        });form.addView(teach);
        Button clearWake=new Button(this);clearWake.setText("Usuń nauczone hasło");
        clearWake.setOnClickListener(v->{wake.forget();teach.setText("🎤 Naucz hasła (0/3)");});
        form.addView(clearWake);
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
                xiaozhi.setBargeIn(interruption.isChecked());
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
        if(req==PERM_MIC)onMicrophone();
        if(req==PERM_WAKE){xiaozhi.stop();main.postDelayed(()->wake.train(),400);}
        if(req==PERM_CAMERA)xiaozhi.previewCamera();
    }
    private void ui(Runnable r){if(!isFinishing())main.post(()->{if(!isFinishing())r.run();});}
    @Override public void state(FaceView.State s){ui(()->{
        if(!sleeping||s==FaceView.State.STANDBY)face.setState(s);
        if(s==FaceView.State.LISTENING||s==FaceView.State.CONNECTING||s==FaceView.State.SPEAKING||s==FaceView.State.THINKING)
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    });}
    @Override public void status(String s){ui(()->face.setStatus(s));}
    @Override public void caption(String s){ui(()->face.setSubtitle(s));}
    @Override public void emotion(String e){ui(()->face.setEmotion(e));}
    @Override public void activation(String c){ui(()->face.setActivationCode(c));}
    @Override public void reaction(String cue){ui(()->face.setReaction(cue));}
    @Override public void photo(byte[] jpeg){ui(()->face.setPhoto(jpeg));}
    @Override public void standby(){ui(this::onStandby);}
    @Override public void level(int value,boolean microphone){
        long now=android.os.SystemClock.uptimeMillis();
        if(now-lastLevelAt<90)return;
        lastLevelAt=now;
        ui(()->{if(microphone)face.setMicLevel(value);else face.setSpeakerLevel(value);});
    }
    @Override protected void onPause(){super.onPause();if(wake!=null)wake.stop();if(xiaozhi!=null)xiaozhi.stop();}
    @Override protected void onResume(){
        super.onResume();
        if(sleeping&&wake!=null&&wake.trained()==3)main.postDelayed(()->{
            if(sleeping&&!isFinishing())wake.listen();
        },800);
        if(face!=null&&xiaozhi!=null)face.setBattery(xiaozhi.controls().battery());
    }
    @Override protected void onDestroy(){
        if(wake!=null)wake.stop();
        if(xiaozhi!=null)xiaozhi.shutdown();
        super.onDestroy();
    }
}
