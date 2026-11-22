package pl.espen.alacontroller;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

/** Independent smartphone-only app. No changes to Espen firmware or watch. */
public final class MainActivity extends Activity {
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final BridgeClient bridge=new BridgeClient();
    private SharedPreferences prefs;
    private FaceView face;
    private TextView status,secondsText;
    private CheckBox delayFive,keepBlush;
    private SeekBar durationBar;
    private Runnable pending;
    private long generation=0;
    private String selected="neutral";

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        getWindow().setStatusBarColor(0xff071020);
        getWindow().setNavigationBarColor(0xff071020);
        prefs=getSharedPreferences("ala_phone",Context.MODE_PRIVATE);
        bridge.configure(prefs.getString("bridge",""),prefs.getString("token",""));
        build();
    }
    private int dp(float pixels){return Math.round(pixels*getResources().getDisplayMetrics().density);}
    private TextView label(String text,int size,int color){
        TextView v=new TextView(this);v.setText(text);v.setTextSize(size);v.setTextColor(color);
        v.setPadding(dp(3),dp(5),dp(3),dp(5));return v;
    }
    private Button button(String text){
        Button b=new Button(this);b.setText(text);b.setAllCaps(false);b.setTextSize(13);
        b.setTextColor(Color.WHITE);return b;
    }
    private void build(){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        scroll.setBackgroundColor(0xff071020);
        LinearLayout content=new LinearLayout(this);
        content.setPadding(dp(13),dp(14),dp(13),dp(22));
        content.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(content);
        TextView header=label("ALA  •  PANEL NA TELEFON",21,0xffe6f5ff);
        header.setGravity(Gravity.CENTER);content.addView(header);
        TextView lead=label("Emocje i rumieńce • osobna aplikacja",12,0xff97b6cf);
        lead.setGravity(Gravity.CENTER);content.addView(lead);
        face=new FaceView(this);
        content.addView(face,new LinearLayout.LayoutParams(-1,dp(275)));
        status=label("Podgląd lokalny na telefonie — robot nie jest połączony.",12,0xff96cde9);
        status.setGravity(Gravity.CENTER);content.addView(status);
        if(bridge.configured())status.setText("Adres zapisany — sprawdź połączenie z serwerem.");
        title(content,"WYBIERZ EMOCJĘ");
        addRow(content,new String[]{"😀 Radość","💗 Miłość","😳 Rumieńce"},
            new String[]{"happy","love","shy"});
        addRow(content,new String[]{"😢 Smutek","😲 Zdziwienie","😠 Złość"},
            new String[]{"sad","surprised","angry"});
        addRow(content,new String[]{"😴 Sen","🙂 Neutralna"},
            new String[]{"sleep","neutral"});
        title(content,"CZAS I EFEKTY");
        delayFive=new CheckBox(this);
        delayFive.setText("Pokaż emocję po 5 sekundach");
        delayFive.setTextColor(0xffd9e9f9);
        delayFive.setChecked(prefs.getBoolean("delay",true));
        delayFive.setOnCheckedChangeListener((b,v)->prefs.edit().putBoolean("delay",v).apply());
        content.addView(delayFive);
        keepBlush=new CheckBox(this);
        keepBlush.setText("Rumieńce / emocja na stałe — aż kliknę X");
        keepBlush.setTextColor(0xffffc7df);
        keepBlush.setChecked(prefs.getBoolean("forever",false));
        keepBlush.setOnCheckedChangeListener((b,v)->prefs.edit().putBoolean("forever",v).apply());
        content.addView(keepBlush);
        int duration=prefs.getInt("duration",47);
        duration=Math.max(1,Math.min(120,duration));
        secondsText=label("Czas reakcji: "+duration+" sekund",14,0xffd9e9f9);
        content.addView(secondsText);
        durationBar=new SeekBar(this);
        durationBar.setMax(119);durationBar.setProgress(duration-1);content.addView(durationBar);
        durationBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar b,int v,boolean fromUser){
                secondsText.setText("Czas reakcji: "+(v+1)+" sekund");
                if(fromUser)prefs.edit().putInt("duration",v+1).apply();
            }
            public void onStartTrackingTouch(SeekBar b){}
            public void onStopTrackingTouch(SeekBar b){}
        });
        Button stop=button("✕   Zatrzymaj emocję i rumieńce");
        stop.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff6e294d));
        stop.setOnClickListener(v->stopEmotion());
        content.addView(stop,new LinearLayout.LayoutParams(-1,dp(56)));
        title(content,"POŁĄCZENIE Z ROBOTEM (OPCJONALNE)");
        Button config=button("⚙  Adres serwera sterującego");
        config.setOnClickListener(v->openConnection());
        content.addView(config);
        Button test=button("🔗  Sprawdź połączenie");
        test.setOnClickListener(v->{
            status.setText("Sprawdzam połączenie...");
            bridge.test((ok,message)->status.setText(message));
        });
        content.addView(test);
        TextView note=label("Bez serwera pośredniczącego wszystkie emocje zmieniają tylko podgląd na telefonie. Sama aplikacja nie może sterować firmware Ali.",11,0xff829bb1);
        note.setPadding(dp(5),dp(12),dp(5),dp(5));
        content.addView(note);
        setContentView(scroll);
    }
    private void title(LinearLayout root,String title){
        TextView v=label(title,12,0xff7cdcf0);
        v.setPadding(dp(5),dp(12),dp(5),dp(7));
        root.addView(v);
    }
    private void addRow(LinearLayout root,String[] labels,String[] moods){
        LinearLayout row=new LinearLayout(this);row.setOrientation(LinearLayout.HORIZONTAL);
        for(int i=0;i<labels.length;i++){
            final String mood=moods[i];
            Button b=button(labels[i]);
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(54),1f);
            lp.setMargins(dp(2),dp(2),dp(2),dp(2));
            row.addView(b,lp);
            b.setOnClickListener(v->schedule(mood));
        }
        root.addView(row);
    }
    private void cancelPending(){generation++;if(pending!=null){ui.removeCallbacks(pending);pending=null;}}
    private void schedule(String mood){
        cancelPending();
        selected=mood;
        final boolean hold=keepBlush.isChecked();
        final boolean blush=hold||"shy".equals(mood)||"love".equals(mood);
        final int seconds=durationBar.getProgress()+1;
        final int delay=delayFive.isChecked()?5:0;
        final long snapshot=generation;
        if(delay>0){
            face.waiting(mood,delay);
            status.setText("Za "+delay+" sekund pokażę emocję na telefonie.");
        }
        pending=()->{
            if(snapshot!=generation)return;
            face.show(mood,blush,hold,seconds);
            String local="Podgląd telefonu: "+name(mood)+(hold?" — na stałe do X":" — "+seconds+" sekund");
            status.setText(local);
            if(bridge.configured()){
                bridge.apply(mood,blush,hold,seconds,0,(ok,message)->{
                    status.setText(local+"\n"+message);
                });
            }
        };
        ui.postDelayed(pending,delay*1000L);
    }
    private String name(String m){
        switch(m){
            case "happy":return "radość";case "love":return "miłość";
            case "shy":return "rumieńce";case "sad":return "smutek";
            case "surprised":return "zdziwienie";case "angry":return "złość";
            case "sleep":return "sen";default:return "neutralna";
        }
    }
    private void stopEmotion(){
        cancelPending();
        face.reset();
        status.setText("Emocja wyłączona. Podgląd neutralny.");
        if(bridge.configured()){
            bridge.apply("neutral",false,false,1,0,(ok,message)->status.setText("Podgląd neutralny. "+message));
        }
    }
    private void openConnection(){
        LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(15),dp(4),dp(15),dp(4));
        TextView hint=label("Wymaga osobnego serwera API. Firmware Espen nie udostępnia jeszcze takiego interfejsu.",12,0xff444444);
        form.addView(hint);
        EditText url=new EditText(this);
        url.setSingleLine(true);url.setTextSize(14);
        url.setHint("np. http://192.168.1.20:8080");
        url.setText(prefs.getString("bridge",""));
        url.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);
        form.addView(url);
        EditText token=new EditText(this);token.setSingleLine(true);
        token.setTextSize(13);token.setHint("Opcjonalny token połączenia");
        token.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD);
        token.setText(prefs.getString("token",""));form.addView(token);
        new AlertDialog.Builder(this)
            .setTitle("Połączenie z Alą")
            .setView(form)
            .setPositiveButton("Zapisz",(dialog,which)->{
                String address=url.getText().toString().trim();
                if(!address.isEmpty()&&!BridgeClient.validUrl(address)){
                    status.setText("Zły adres: użyj HTTPS lub prywatnego LAN HTTP.");
                    return;
                }
                prefs.edit().putString("bridge",address).putString("token",token.getText().toString()).apply();
                bridge.configure(address,token.getText().toString());
                status.setText(address.isEmpty()?"Tylko podgląd lokalny.":"Adres zapisany. Naciśnij Sprawdź połączenie.");
            })
            .setNeutralButton("Odłącz",(dialog,which)->{
                prefs.edit().remove("bridge").remove("token").apply();
                bridge.configure("","");
                status.setText("Odłączono. Emocje są wyłącznie na telefonie.");
            })
            .setNegativeButton("Anuluj",null).show();
    }
    @Override protected void onDestroy(){
        cancelPending();bridge.close();super.onDestroy();
    }
}
