package pl.homemind.alapanel;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.text.InputType;
import android.view.View;
import android.view.Gravity;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int BACKGROUND = 0xff0b121c;
    private static final int CARD = 0xff172536;
    private static final int INK = 0xffeff6fc;
    private static final int SECONDARY = 0xffbacddd;
    private static final int ACCENT = 0xff75e5d7;
    private static final int PINK = 0xffff99c9;
    private final ExecutorService work = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private EditText hostField, pinField, speechField;
    private CheckBox delayCheck, onlineCheck;
    private TextView statusView, emotionView;
    private RadioGroup durationGroup;
    private TextToSpeech tts;
    private boolean ttsReady = false;
    private boolean synthesizing = false;
    private String speechHost = "", speechPin = "";
    private final String[] names = {"neutral","happy","laugh","sad","surprised","angry",
            "thinking","sleepy","love","confident","shy","confused","fear","wink","bored","excited"};
    private final String[] labels = {"😐 Spokój","😊 Radość","😆 Śmiech","😢 Smutek",
            "😲 Zdziwienie","😠 Złość","🤔 Myślenie","😴 Senność",
            "😍 Miłość","😎 Pewność","😊 Rumieńce","😵 Zmieszanie",
            "😨 Strach","😉 Mrugnięcie","😒 Nuda","🤩 Ekscytacja"};

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(BACKGROUND);
        getWindow().setNavigationBarColor(BACKGROUND);
        buildUi();
        tts = new TextToSpeech(this, result -> {
            if (result == TextToSpeech.SUCCESS) {
                int language = tts.setLanguage(new Locale("pl", "PL"));
                ttsReady = language != TextToSpeech.LANG_MISSING_DATA &&
                           language != TextToSpeech.LANG_NOT_SUPPORTED;
                tts.setSpeechRate(0.94f);
                tts.setPitch(1.05f);
                if (!ttsReady) tell("Zainstaluj polski głos TTS w ustawieniach Androida.");
            } else tell("Nie udało się uruchomić syntezatora mowy Androida.");
        });
        tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
            @Override public void onStart(String utteranceId) { tell("Tworzę głos Ali…"); }
            @Override public void onDone(String utteranceId) {
                File file = new File(getCacheDir(), utteranceId + ".wav");
                String host = speechHost, pin = speechPin;
                work.execute(() -> {
                    try {
                        byte[] pcm = wavTo24kPcm(file);
                        tell("Wysyłam głos do Ali…");
                        send(host,pin,"POST","/api/speech",pcm,"application/octet-stream",40000);
                        tell("Ala wypowiedziała zdanie ✓");
                    } catch (Exception e) {
                        tell("Nie udało się odtworzyć: " + readable(e));
                    } finally {
                        file.delete();
                        main.post(() -> synthesizing = false);
                    }
                });
            }
            @Override public void onError(String utteranceId) {
                main.post(() -> synthesizing = false);
                tell("Błąd polskiego syntezatora mowy — sprawdź ustawienia TTS.");
            }
        });
    }

    private void buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20),dp(28),dp(20),dp(32));
        scroll.addView(root);
        setContentView(scroll);
        TextView tag = text("HOMEMIND  /  ALA",15,ACCENT,true);
        root.addView(tag);
        root.addView(text("Panel Ali ♡",34,INK,true));
        root.addView(text("Steruj buźką robota i wyślij do niego własne zdanie.",16,SECONDARY,false));
        LinearLayout config = card(root);
        config.addView(text("Połączenie z Alą",21,INK,true));
        config.addView(text("Telefon i Ala muszą być w tej samej sieci Wi-Fi.",14,SECONDARY,false));
        SharedPreferences prefs = getSharedPreferences("ala",MODE_PRIVATE);
        hostField = field(config,"Adres IP Ali, np. 192.168.1.120",prefs.getString("host",""));
        pinField = field(config,"Kod z Ustawień Ali (6 cyfr)",prefs.getString("pin",""));
        pinField.setInputType(InputType.TYPE_CLASS_NUMBER);
        button(config,"Sprawdź połączenie",true,() -> {
            Connection c = connection();
            if(c==null)return;
            work.execute(() -> {
                try {
                    String result = send(c.host,c.pin,"GET","/api/status",null,null,7000);
                    JSONObject json = new JSONObject(result);
                    tell(json.optBoolean("ready",false) ?
                        "Połączono z Alą ✓ Jest gotowa." :
                        "Połączono z Alą ✓ Teraz jest zajęta rozmową.");
                } catch (Exception e) {tell("Brak połączenia: "+readable(e));}
            });
        });
        LinearLayout emotions = card(root);
        emotions.addView(text("Emocje Ali",22,INK,true));
        emotions.addView(text("Dotknij emocji, żeby pojawiła się na ekranie robota.",14,SECONDARY,false));
        durationGroup = new RadioGroup(this);
        durationGroup.setOrientation(RadioGroup.HORIZONTAL);
        addDuration(durationGroup,"5 s",5000,false);
        addDuration(durationGroup,"47 s",47000,true);
        addDuration(durationGroup,"Na stałe",0,false);
        emotions.addView(durationGroup);
        delayCheck = new CheckBox(this);
        delayCheck.setText("Pokaż emocję po 5 sekundach");
        delayCheck.setTextColor(INK);
        emotions.addView(delayCheck);
        GridLayout grid = new GridLayout(this);
        grid.setColumnCount(2);
        emotions.addView(grid,new LinearLayout.LayoutParams(-1,-2));
        for(int i=0;i<names.length;i++) {
            final int idx=i;
            Button b = new Button(this);
            b.setAllCaps(false);
            b.setText(labels[i]);
            b.setTextSize(14);
            b.setTextColor(BACKGROUND);
            b.setBackgroundTintList(ColorStateList.valueOf(i==8||i==10?PINK:ACCENT));
            GridLayout.LayoutParams lp = new GridLayout.LayoutParams();
            lp.width=0;lp.height=dp(66);
            lp.columnSpec=GridLayout.spec(GridLayout.UNDEFINED,1,1f);
            lp.setMargins(dp(3),dp(3),dp(3),dp(3));
            grid.addView(b,lp);
            b.setOnClickListener(v -> setEmotion(idx));
        }
        emotionView = text("Wybierz emocję ↑",14,SECONDARY,false);
        emotions.addView(emotionView);
        LinearLayout speaker = card(root);
        speaker.addView(text("Niech Ala to powie",22,INK,true));
        speaker.addView(text("Wpisz zdanie. Poprawimy pisownię i wyślemy głos do głośnika Ali.",14,SECONDARY,false));
        speechField = field(speaker,"Np. Cześć! Mam dla was super wiadomość!", "");
        speechField.setSingleLine(false);
        speechField.setMinLines(3);
        speechField.setGravity(Gravity.TOP);
        onlineCheck = new CheckBox(this);
        onlineCheck.setText("Pełna korekta online (LanguageTool) — wyślij tekst do usługi");
        onlineCheck.setTextSize(13);
        onlineCheck.setTextColor(SECONDARY);
        speaker.addView(onlineCheck);
        button(speaker,"Popraw zdanie",false,() -> correct(false));
        button(speaker,"✨ Popraw i wypowiedz przez Alę",true,() -> correct(true));
        speaker.addView(text("Bez zgody na korektę online działa tylko podstawowe poprawianie spacji, wielkiej litery i interpunkcji. Głos tworzy syntezator Androida; po Wi-Fi trafia do Ali.",13,SECONDARY,false));
        statusView = text("Gotowe. Wpisz adres i kod z ekranu Ali.",14,ACCENT,false);
        root.addView(statusView);
    }

    private void addDuration(RadioGroup group,String caption,int value,boolean checked) {
        RadioButton button = new RadioButton(this);
        button.setText(caption);
        button.setTextColor(INK);
        button.setTextSize(13);
        button.setTag(value);
        button.setId(View.generateViewId());
        group.addView(button);
        button.setChecked(checked);
    }
    private LinearLayout card(LinearLayout root) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(16),dp(17),dp(16),dp(18));
        panel.setBackgroundColor(CARD);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);
        p.topMargin=dp(20);
        root.addView(panel,p);
        return panel;
    }
    private TextView text(String value,int size,int color,boolean bold) {
        TextView t=new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        if(bold)t.setTypeface(null,Typeface.BOLD);
        t.setPadding(0,dp(4),0,dp(7));
        return t;
    }
    private EditText field(LinearLayout root,String hint,String saved) {
        EditText edit=new EditText(this);
        edit.setHint(hint);
        edit.setText(saved);
        edit.setTextSize(16);
        edit.setSingleLine(true);
        edit.setTextColor(INK);
        edit.setHintTextColor(SECONDARY);
        edit.setPadding(dp(8),dp(9),dp(8),dp(9));
        root.addView(edit,new LinearLayout.LayoutParams(-1,-2));
        return edit;
    }
    private void button(LinearLayout root,String caption,boolean mainAction,Runnable action) {
        Button b=new Button(this);
        b.setAllCaps(false);
        b.setText(caption);
        b.setTextColor(BACKGROUND);
        b.setTextSize(16);
        b.setBackgroundTintList(ColorStateList.valueOf(mainAction?ACCENT:PINK));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(56));
        lp.topMargin=dp(10);
        root.addView(b,lp);
        b.setOnClickListener(v -> action.run());
    }
    private int dp(float value) { return (int)(value*getResources().getDisplayMetrics().density+0.5f); }
    private void tell(String value) {main.post(() -> statusView.setText(value));}

    private static final class Connection {
        final String host,pin;
        Connection(String host,String pin){this.host=host;this.pin=pin;}
    }
    private Connection connection() {
        String host=hostField.getText().toString().trim();
        String pin=pinField.getText().toString().trim();
        String[] bits=host.split("\\.");
        boolean valid=bits.length==4;
        if(valid)for(String part:bits) {
            try {int number=Integer.parseInt(part);if(number<0||number>255)valid=false;}
            catch(NumberFormatException e){valid=false;}
        }
        if(!valid||!pin.matches("[0-9]{6}")){
            tell("Wpisz poprawny adres IPv4 i sześciocyfrowy kod z Ali.");
            return null;
        }
        getSharedPreferences("ala",MODE_PRIVATE).edit().putString("host",host).putString("pin",pin).apply();
        return new Connection(host,pin);
    }
    private void setEmotion(int index) {
        Connection c=connection();if(c==null)return;
        int duration=47000;
        for(int i=0;i<durationGroup.getChildCount();i++) {
            RadioButton r=(RadioButton)durationGroup.getChildAt(i);
            if(r.isChecked())duration=(Integer)r.getTag();
        }
        int delay=delayCheck.isChecked()?5000:0;
        // Neutral always cancels the override immediately.
        if(index==0)delay=0;
        final int selectedDuration=duration,selectedDelay=delay;
        emotionView.setText("Wybrano: "+labels[index]);
        work.execute(() -> {
            try {
                JSONObject payload=new JSONObject().put("emotion",names[index])
                    .put("delay_ms",selectedDelay).put("duration_ms",selectedDuration);
                send(c.host,c.pin,"POST","/api/emotion",
                     payload.toString().getBytes(StandardCharsets.UTF_8),"application/json",7000);
                tell(index==0?"Powrót do normalnej mimiki ✓":
                    "Wysłano emocję "+labels[index]+(selectedDelay>0?" (po 5 s)":"")+" ✓");
            } catch(Exception e){tell("Nie wysłano emocji: "+readable(e));}
        });
    }
    private static String normalize(String input) {
        String t=input.trim().replaceAll("\\s+"," ");
        if(t.isEmpty())return t;
        t=t.substring(0,1).toUpperCase(new Locale("pl","PL"))+t.substring(1);
        if(!t.matches("(?s).*[.!?…]$"))t+=".";
        return t;
    }
    private void correct(boolean say) {
        String input=speechField.getText().toString().trim();
        if(input.isEmpty()){tell("Najpierw wpisz zdanie.");return;}
        if(input.length()>360){tell("Maksymalnie 360 znaków na jedną wypowiedź.");return;}
        if(say && synthesizing){tell("Trwa przygotowanie poprzedniej wypowiedzi.");return;}
        Connection c=say?connection():null;
        if(say&&c==null)return;
        boolean online=onlineCheck.isChecked();
        tell(online?"Sprawdzam pisownię online…":"Poprawiam podstawowe formatowanie…");
        work.execute(() -> {
            try {
                String fixed=online?languageTool(input):normalize(input);
                main.post(() -> {
                    speechField.setText(fixed);
                    if(!say) {
                        tell(online?"Poprawiono pisownię ✓":"Zastosowano podstawową korektę ✓");
                    } else synthesize(fixed,c);
                });
            } catch(Exception e) {
                tell("Nie udało się poprawić tekstu: "+readable(e)+". Możesz wyłączyć korektę online.");
            }
        });
    }
    private static String languageTool(String original) throws Exception {
        byte[] data=("language=pl-PL&text="+URLEncoder.encode(original,"UTF-8"))
            .getBytes(StandardCharsets.UTF_8);
        HttpURLConnection conn=(HttpURLConnection)new URL("https://api.languagetool.org/v2/check").openConnection();
        conn.setRequestMethod("POST");
        conn.setConnectTimeout(8000);
        conn.setReadTimeout(12000);
        conn.setDoOutput(true);
        conn.setFixedLengthStreamingMode(data.length);
        conn.setRequestProperty("Content-Type","application/x-www-form-urlencoded; charset=utf-8");
        try {
            try(OutputStream out=conn.getOutputStream()){out.write(data);}
            if(conn.getResponseCode()!=200)throw new IOException("usługa korekty HTTP "+conn.getResponseCode());
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            try(java.io.InputStream in=conn.getInputStream()){
                byte[] chunk=new byte[4096];int n;
                while((n=in.read(chunk))!=-1){out.write(chunk,0,n);if(out.size()>160000)throw new IOException("zbyt duża odpowiedź");}
            }
            JSONObject result=new JSONObject(out.toString("UTF-8"));
            JSONArray matches=result.optJSONArray("matches");
            if(matches==null)return normalize(original);
            JSONObject[] items=new JSONObject[matches.length()];
            for(int i=0;i<items.length;i++)items[i]=matches.getJSONObject(i);
            Arrays.sort(items,Comparator.comparingInt((JSONObject o)->o.optInt("offset")).reversed());
            StringBuilder fixed=new StringBuilder(original);
            for(JSONObject item:items) {
                int start=item.optInt("offset",-1),length=item.optInt("length",-1);
                JSONArray replacements=item.optJSONArray("replacements");
                if(start<0||length<0||start+length>fixed.length()||
                   replacements==null||replacements.length()==0)continue;
                String value=replacements.getJSONObject(0).optString("value","");
                if(value.length()<=80)fixed.replace(start,start+length,value);
            }
            return normalize(fixed.toString());
        } finally {conn.disconnect();}
    }

    private void synthesize(String sentence,Connection c) {
        if(!ttsReady){tell("Włącz polski głos TTS w ustawieniach Androida.");return;}
        if(synthesizing){tell("Ala przygotowuje już inne zdanie.");return;}
        synthesizing=true;
        speechHost=c.host;speechPin=c.pin;
        String id="ala_"+System.currentTimeMillis();
        File file=new File(getCacheDir(),id+".wav");
        int result=tts.synthesizeToFile(sentence,new Bundle(),file,id);
        if(result!=TextToSpeech.SUCCESS) {
            synthesizing=false;
            tell("Syntezator mowy nie przyjął zdania.");
        }
    }

    private static int u16(byte[] b,int pos) {
        return (b[pos]&255)|((b[pos+1]&255)<<8);
    }
    private static long u32(byte[] b,int pos) {
        return ((long)b[pos]&255)|(((long)b[pos+1]&255)<<8)|
               (((long)b[pos+2]&255)<<16)|(((long)b[pos+3]&255)<<24);
    }
    private static boolean tag(byte[] b,int pos,String value) {
        return pos+4<=b.length && value.equals(new String(b,pos,4,StandardCharsets.US_ASCII));
    }
    private static byte[] wavTo24kPcm(File file) throws IOException {
        if(!file.exists()||file.length()>3000000)throw new IOException("Brak pliku lub głos za długi");
        byte[] wav;
        try(FileInputStream in=new FileInputStream(file)){
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            byte[] data=new byte[8192];int n;
            while((n=in.read(data))!=-1)out.write(data,0,n);
            wav=out.toByteArray();
        }
        if(wav.length<44||!tag(wav,0,"RIFF")||!tag(wav,8,"WAVE"))
            throw new IOException("Syntezator nie zwrócił dźwięku WAV");
        int channels=0,sampleRate=0,format=0,bits=0,offset=-1,length=0;
        for(int pos=12;pos+8<=wav.length;) {
            long chunkSize=u32(wav,pos+4);
            if(chunkSize>wav.length-pos-8)throw new IOException("Uszkodzony WAV");
            int len=(int)chunkSize;
            if(tag(wav,pos,"fmt ")&&len>=16) {
                format=u16(wav,pos+8);channels=u16(wav,pos+10);
                sampleRate=(int)u32(wav,pos+12);
                bits=u16(wav,pos+22);
            }
            if(tag(wav,pos,"data")){offset=pos+8;length=len;break;}
            pos+=8+len+(len&1);
        }
        if(format!=1||bits!=16||(channels!=1&&channels!=2)||sampleRate<8000||
           sampleRate>96000||offset<0||length<2*channels)
            throw new IOException("Nieobsługiwany format głosu (wymagany WAV PCM16)");
        int frames=length/(2*channels);
        int outFrames=(int)((long)frames*24000/sampleRate);
        if(outFrames<=0||outFrames>24000*25)
            throw new IOException("Wypowiedź może trwać najwyżej 25 sekund");
        byte[] pcm=new byte[outFrames*2];
        for(int i=0;i<outFrames;i++) {
            double position=(double)i*sampleRate/24000.0;
            int lo=Math.min(frames-1,(int)position),hi=Math.min(frames-1,lo+1);
            double fraction=position-lo;
            int a=monoSample(wav,offset,channels,lo),b=monoSample(wav,offset,channels,hi);
            int value=(int)Math.round(a+(b-a)*fraction);
            pcm[2*i]=(byte)(value&255);
            pcm[2*i+1]=(byte)((value>>8)&255);
        }
        return pcm;
    }
    private static int monoSample(byte[] data,int offset,int channels,int frame) {
        int pos=offset+frame*channels*2;
        short a=(short)u16(data,pos);
        if(channels==1)return a;
        short b=(short)u16(data,pos+2);
        return (a+b)/2;
    }
    private static String send(String host,String pin,String method,String path,
                               byte[] payload,String mime,int timeout) throws Exception {
        HttpURLConnection conn=(HttpURLConnection)new URL("http://"+host+":8787"+path).openConnection();
        conn.setRequestMethod(method);
        conn.setConnectTimeout(6000);
        conn.setReadTimeout(timeout);
        conn.setRequestProperty("X-Ala-Pin",pin);
        if(payload!=null) {
            conn.setDoOutput(true);
            conn.setFixedLengthStreamingMode(payload.length);
            conn.setRequestProperty("Content-Type",mime);
        }
        try {
            if(payload!=null)try(OutputStream out=conn.getOutputStream()){out.write(payload);}
            int code=conn.getResponseCode();
            if(code!=200)throw new IOException(code==401?"zły kod parowania":
                code==409?"Ala jest zajęta lub głośnik jest wyciszony":"HTTP "+code);
            try(java.io.InputStream in=conn.getInputStream()){
                ByteArrayOutputStream out=new ByteArrayOutputStream();
                byte[] buf=new byte[512];int n;
                while((n=in.read(buf))!=-1) {out.write(buf,0,n);if(out.size()>4000)break;}
                return out.toString("UTF-8");
            }
        } finally {conn.disconnect();}
    }
    private static String readable(Exception e){
        return e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
    }
    @Override protected void onDestroy() {
        if(tts!=null){tts.stop();tts.shutdown();}
        work.shutdownNow();
        super.onDestroy();
    }
}
