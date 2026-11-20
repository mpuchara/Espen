package pl.espen.garett;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Phone-only control panel. Controls the Ala face rendered IN THIS APP;
 * it deliberately does not claim to control a remote ESP32 or watch.
 */
final class PhoneEmotionPanel {
    private final Activity activity;
    private final FaceView face;
    private final WatchControls phone;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private Runnable pending;
    private final String[][] emotions={
        {"😍 Zakochana","love"},{"😊 Wesoła","happy"},
        {"😳 Rumieńce","blush"},{"😂 Śmiech","laugh"},
        {"😮 Zaskoczona","surprised"},{"😢 Smutna","sad"},
        {"😠 Zła","angry"},{"😉 Mrugnięcie","wink"},
        {"😴 Śpiąca","sleep"},{"🤔 Myśląca","thinking"},
        {"🎉 Zwycięstwo","victory"},{"💡 Eureka","eureka"},
        {"🕵 Detektyw","detective"},{"🤖 Robot","robot"},
        {"💃 Taniec","dance"},{"🤫 Sekret","secret"}
    };
    PhoneEmotionPanel(Activity a,FaceView f,WatchControls c){activity=a;face=f;phone=c;}
    void show(){
        ScrollView scroll=new ScrollView(activity);
        LinearLayout root=new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(10),dp(8),dp(10),dp(12));
        scroll.addView(root);
        TextView heading=new TextView(activity);
        heading.setText("Wybierz emocję. Ala pokaże ją po 5 sekundach.\nRumieńce i zakochanie pozostają do naciśnięcia X.");
        heading.setTextSize(15);
        heading.setPadding(dp(5),0,dp(5),dp(10));
        root.addView(heading);
        GridLayout grid=new GridLayout(activity);
        grid.setColumnCount(2);root.addView(grid);
        for(String[] row:emotions){
            Button b=new Button(activity);b.setText(row[0]);b.setTextSize(12);b.setAllCaps(false);
            GridLayout.LayoutParams p=new GridLayout.LayoutParams();
            p.width=0;p.columnSpec=GridLayout.spec(GridLayout.UNDEFINED,1,1f);
            p.setMargins(dp(2),dp(2),dp(2),dp(2));
            grid.addView(b,p);
            b.setOnClickListener(v->queue(row[1]));
        }
        Button cancel=new Button(activity);
        cancel.setText("✕ Usuń emocję i rumieńce");
        cancel.setOnClickListener(v->{
            if(pending!=null)handler.removeCallbacks(pending);
            pending=null;face.resetPhoneEmotion();
            Toast.makeText(activity,"Ala wróciła do normalnej miny",Toast.LENGTH_SHORT).show();
        });root.addView(cancel);
        Button controls=new Button(activity);
        controls.setText("⚙ Sterowanie tym telefonem");
        controls.setOnClickListener(v->phone.showPanel());
        root.addView(controls);
        new AlertDialog.Builder(activity)
            .setTitle("Panel Ali na telefonie")
            .setView(scroll)
            .setPositiveButton("Zamknij",null)
            .show();
    }
    private void queue(String emotion){
        if(pending!=null)handler.removeCallbacks(pending);
        Toast.makeText(activity,"Ala pokaże emocję za 5 sekund",Toast.LENGTH_SHORT).show();
        pending=()->{
            if("blush".equals(emotion)){
                face.setBlush(true);
                face.setEmotionPersistent("shy");
            }else if("love".equals(emotion)){
                face.setBlush(true);
                face.setEmotionPersistent("love");
            }else{
                face.resetPhoneEmotion();
                face.setEmotion(emotion);
                face.setReaction(emotion);
            }
            pending=null;
        };
        handler.postDelayed(pending,5000);
    }
    void close(){
        if(pending!=null)handler.removeCallbacks(pending);
        pending=null;
    }
    private int dp(int v){return (int)(v*activity.getResources().getDisplayMetrics().density+.5f);}
}
