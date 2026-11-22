package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.media.AudioManager;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Vibrator;
import android.provider.AlarmClock;
import android.provider.Settings;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Capability-aware Android 8.1 watch controls. Never uses hidden APIs, root,
 * device administrator, or vendor SOS/parental configuration.
 */
@SuppressWarnings("deprecation")
final class WatchControls {
    private final Activity activity;
    private boolean torch;
    private String torchId;
    private int brightness=75;

    WatchControls(Activity activity){ this.activity=activity; }
    static int clamp(int n){return Math.max(0,Math.min(100,n));}
    int volume(){
        AudioManager m=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
        return m==null?0:100*m.getStreamVolume(AudioManager.STREAM_MUSIC)/
            Math.max(1,m.getStreamMaxVolume(AudioManager.STREAM_MUSIC));
    }
    void setVolume(int level){
        AudioManager m=(AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
        if(m!=null)m.setStreamVolume(AudioManager.STREAM_MUSIC,
            Math.max(0,m.getStreamMaxVolume(AudioManager.STREAM_MUSIC)*clamp(level)/100),0);
    }
    void setBrightness(int level){
        brightness=Math.max(10,clamp(level)); // avoid a black, unrecoverable watch screen
        activity.runOnUiThread(()->{
            WindowManager.LayoutParams p=activity.getWindow().getAttributes();
            p.screenBrightness=brightness/100f;activity.getWindow().setAttributes(p);
        });
    }
    int brightness(){return brightness;}
    void vibrate(int ms){
        Vibrator v=(Vibrator)activity.getSystemService(Context.VIBRATOR_SERVICE);
        if(v!=null&&v.hasVibrator())v.vibrate(Math.min(700,Math.max(30,ms)));
    }
    int battery(){
        Intent status=activity.registerReceiver(null,new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if(status==null)return -1;
        int level=status.getIntExtra(BatteryManager.EXTRA_LEVEL,-1);
        int scale=status.getIntExtra(BatteryManager.EXTRA_SCALE,100);
        return level<0?-1:level*100/Math.max(1,scale);
    }
    String status(){
        String wifi="nieznane";
        try{
            WifiManager w=(WifiManager)activity.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if(w!=null)wifi=w.isWifiEnabled()?"ON":"OFF";
        }catch(Exception ignored){}
        String bt="brak";
        try{
            BluetoothAdapter a=BluetoothAdapter.getDefaultAdapter();
            if(a!=null)bt=a.isEnabled()?"ON":"OFF";
        }catch(Exception ignored){}
        return "Bateria: "+battery()+"%, multimedia: "+volume()+"%, jasność Ali: "+
            brightness+"%, Wi-Fi: "+wifi+", Bluetooth: "+bt+", latarka: "+(torch?"ON":"OFF")+
            ". Systemowe ustawienia telefonu pozostają pod kontrolą użytkownika.";
    }
    synchronized String torch(boolean on){
        if(activity.checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED)
            return "Do latarki potrzebna jest zgoda na aparat w ustawieniach aplikacji.";
        try{
            CameraManager m=(CameraManager)activity.getSystemService(Context.CAMERA_SERVICE);
            if(m==null)return "Brak CameraManager";
            if(torchId==null){
                for(String id:m.getCameraIdList()){
                    Boolean flash=m.getCameraCharacteristics(id).get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                    if(Boolean.TRUE.equals(flash)){torchId=id;break;}
                }
            }
            if(torchId==null)return "Ten zegarek nie zgłasza lampy błyskowej.";
            m.setTorchMode(torchId,on);torch=on;
            return on?"Latarka włączona":"Latarka wyłączona";
        }catch(Exception e){torch=false;return "Latarka niedostępna: "+e.getClass().getSimpleName();}
    }
    void open(String item){
        String action="bluetooth".equals(item)?Settings.ACTION_BLUETOOTH_SETTINGS:
            "wifi".equals(item)?Settings.ACTION_WIFI_SETTINGS:
            "location".equals(item)?Settings.ACTION_LOCATION_SOURCE_SETTINGS:
            Settings.ACTION_SETTINGS;
        activity.runOnUiThread(()->{
            try{activity.startActivity(new Intent(action));}
            catch(Exception e){Toast.makeText(activity,"Brak ekranu ustawień w tym zegarku",Toast.LENGTH_LONG).show();}
        });
    }
    void startTimer(int seconds){
        int n=Math.min(24*3600,Math.max(1,seconds));
        activity.runOnUiThread(()->{
            try{
                Intent i=new Intent(AlarmClock.ACTION_SET_TIMER)
                    .putExtra(AlarmClock.EXTRA_LENGTH,n)
                    .putExtra(AlarmClock.EXTRA_SKIP_UI,false);
                activity.startActivity(i);
            }catch(Exception e){Toast.makeText(activity,"Brak obsługiwanej aplikacji minutnika",Toast.LENGTH_LONG).show();}
        });
    }
    void showPanel(){
        ScrollView sc=new ScrollView(activity);
        LinearLayout l=new LinearLayout(activity);l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(20,10,20,12);sc.addView(l);
        TextView state=new TextView(activity);state.setText(status());state.setTextSize(12);l.addView(state);
        TextView vl=new TextView(activity);vl.setText("Głośność multimediów");l.addView(vl);
        SeekBar v=new SeekBar(activity);v.setMax(100);v.setProgress(volume());l.addView(v);
        v.setOnSeekBarChangeListener(listener(this::setVolume));
        TextView bl=new TextView(activity);bl.setText("Jasność ekranu Ali (tylko w aplikacji)");l.addView(bl);
        SeekBar b=new SeekBar(activity);b.setMax(100);b.setProgress(brightness);l.addView(b);
        b.setOnSeekBarChangeListener(listener(this::setBrightness));
        button(l,"Wibracja – test",()->vibrate(180));
        button(l,"Latarka WŁ./WYŁ.",()->{
            String response=torch(!torch);Toast.makeText(activity,response,Toast.LENGTH_SHORT).show();
        });
        button(l,"Ustawienia Wi-Fi",()->open("wifi"));
        button(l,"Ustawienia Bluetooth",()->open("bluetooth"));
        button(l,"Ustawienia GPS",()->open("location"));
        button(l,"Ustaw minutnik 5 minut",()->startTimer(300));
        button(l,"Telefon – otwórz wybieranie numeru",()->{
            try{activity.startActivity(new Intent(Intent.ACTION_DIAL));}
            catch(Exception e){Toast.makeText(activity,"Brak dialera",Toast.LENGTH_SHORT).show();}
        });
        new AlertDialog.Builder(activity).setTitle("Sterowanie telefonem")
            .setView(sc).setPositiveButton("Gotowe",null).show();
    }
    private interface Change {void apply(int v);}
    private SeekBar.OnSeekBarChangeListener listener(Change change){
        return new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar s,int value,boolean fromUser){if(fromUser)change.apply(value);}
            public void onStartTrackingTouch(SeekBar s){}
            public void onStopTrackingTouch(SeekBar s){}
        };
    }
    private void button(LinearLayout l,String label,Runnable r){
        Button b=new Button(activity);b.setText(label);b.setTextSize(12);
        b.setOnClickListener(v->r.run());l.addView(b);
    }
    static JSONArray tools() throws JSONException {
        JSONArray a=new JSONArray();
        a.put(simple("self.watch.get_status","Sprawdź baterię, głośność, Wi-Fi, Bluetooth, jasność i latarkę",
            new JSONObject()));
        a.put(simple("self.watch.set_brightness","Ustaw jasność ekranu Ali 10–100% (nie całego systemu)",
            number("brightness",10,100)));
        a.put(simple("self.watch.set_volume","Ustaw głośność multimediów zegarka 0–100%",
            number("volume",0,100)));
        a.put(simple("self.watch.vibrate","Krótka wibracja zegarka 30–700 ms",
            number("milliseconds",30,700)));
        a.put(simple("self.watch.set_flashlight","Włącz lub wyłącz latarkę, jeśli zegarek ją posiada",
            new JSONObject().put("enabled",new JSONObject().put("type","boolean"))));
        a.put(simple("self.watch.set_timer","Otwórz minutnik i pokaż użytkownikowi potwierdzenie ustawień",
            number("seconds",1,86400)));
        a.put(simple("self.watch.open_settings","Otwórz ekran ustawień Wi-Fi, Bluetooth lub GPS",
            new JSONObject().put("page",new JSONObject().put("type","string")
                .put("enum",new JSONArray().put("wifi").put("bluetooth").put("location")))));
        return a;
    }
    private static JSONObject number(String k,int min,int max) throws JSONException{
        return new JSONObject().put(k,new JSONObject().put("type","integer")
            .put("minimum",min).put("maximum",max));
    }
    private static JSONObject simple(String name,String desc,JSONObject props) throws JSONException{
        return new JSONObject().put("name",name).put("description",desc)
            .put("inputSchema",new JSONObject().put("type","object").put("properties",props));
    }
    static boolean handles(String name){return name!=null&&name.startsWith("self.watch.");}
    String command(String name,JSONObject args){
        switch(name){
            case "self.watch.get_status":return status();
            case "self.watch.set_volume":setVolume(args.optInt("volume",75));return "Głośność "+volume()+"%";
            case "self.watch.set_brightness":
                setBrightness(args.optInt("brightness",75));return "Jasność Ali "+brightness+"%";
            case "self.watch.vibrate":vibrate(args.optInt("milliseconds",150));return "Wibracja OK";
            case "self.watch.set_flashlight":return torch(args.optBoolean("enabled",false));
            case "self.watch.set_timer":startTimer(args.optInt("seconds",300));
                return "Otwieram minutnik – zatwierdź na ekranie zegarka";
            case "self.watch.open_settings":
                String page=args.optString("page","wifi");
                if(!page.equals("wifi")&&!page.equals("bluetooth")&&!page.equals("location"))
                    return "Dostępne: wifi, bluetooth, location";
                open(page);return "Otwieram ustawienia "+page;
            default:return "Nieznane polecenie zegarka";
        }
    }
}
