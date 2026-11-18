package pl.homemind.ala.phone;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.text.InputFilter;
import android.text.InputType;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.inputmethod.InputMethodManager;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(8,15,28), CARD = Color.rgb(18,33,50);
    private static final int LIGHT = Color.rgb(237,246,255), MUTED = Color.rgb(172,192,210);
    private static final int ACCENT = Color.rgb(121,227,212), BUTTON_BG = Color.rgb(38,59,80);
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private SharedPreferences prefs;
    private EditText ip, pin, sentence, robotName;
    private TextView connectStatus, moodStatus, speechStatus, reply, nameStatus, titleView;
    private CheckBox sticky;
    private Button speakButton;
    private int pollCount = 0;
    private boolean polling = false;
    private boolean nextHello = false;
    private final String[] moods = {"happy","laugh","sad","surprise","angry","thinking","sleepy","love",
            "confident","shy","confused","fear","wink","bored","excited","neutral"};
    private final String[] titles = {"😄 Radość","😂 Śmiech","😢 Smutek","😲 Zdziwienie",
            "😠 Złość","🤔 Myślenie","😴 Senność","😍 Zakochanie","😎 Pewność",
            "😊 Rumieńce","😕 Zmieszanie","😨 Strach","😉 Oczko","😐 Nuda",
            "🤩 Ekscytacja","🙂 Spokój"};

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        prefs=getSharedPreferences("ala_phone",MODE_PRIVATE);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(24), dp(16), dp(42));
        scroll.addView(root);
        setContentView(scroll);

        titleView=text("💙 Pilot Ali", 30, LIGHT, true);
        root.addView(titleView);
        root.addView(text("Sterowanie emocjami i głosem Ali • Wi-Fi", 14, MUTED, false));

        LinearLayout settings = card(root);
        settings.addView(text("🔐 Połączenie", 20, LIGHT, true));
        settings.addView(text("Wpisz IP widoczne na ekranie Ali w Ustawieniach. Telefon i robot muszą być w tej samej sieci Wi-Fi.",14,MUTED,false));
        ip=edit("Adres IP, np. 192.168.1.80", prefs.getString("ip",""), InputType.TYPE_CLASS_PHONE);
        settings.addView(ip);
        pin=edit("8-cyfrowy kod z ekranu Ali",prefs.getString("pin",""),InputType.TYPE_CLASS_NUMBER|InputType.TYPE_NUMBER_VARIATION_PASSWORD);
        pin.setFilters(new InputFilter[]{new InputFilter.LengthFilter(8)});
        settings.addView(pin);
        Button connect = action("Połącz z Alą", true);
        settings.addView(connect);
        connectStatus=text("Najpierw połącz się z Alą.",14,ACCENT,false);
        settings.addView(connectStatus);
        settings.addView(text("✏️ Zmień imię robota",18,LIGHT,true));
        settings.addView(text("Nowe imię zostanie zapisane na robocie. Hasło głosowego wybudzania pozostanie bez zmian.",13,MUTED,false));
        robotName=edit("Nowe imię, np. Zosia","Ala",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_CAP_WORDS);
        robotName.setFilters(new InputFilter[]{new InputFilter.LengthFilter(48)});
        settings.addView(robotName);
        Button saveRobotName=action("💾 Zapisz imię",false);
        settings.addView(saveRobotName);
        nameStatus=text("Po połączeniu odczytam imię robota.",14,ACCENT,false);
        settings.addView(nameStatus);
        saveRobotName.setOnClickListener(v -> {
            String candidate=robotName.getText().toString().trim();
            if(!validRobotName(candidate)){
                nameStatus.setText("Imię musi mieć 2–24 litery, może zawierać spację lub łącznik.");
                return;
            }
            saveRobotName.setEnabled(false);
            nameStatus.setText("Zapisuję...");
            call("/api/name","POST",candidate,
                ok->call("/api/name","GET",null,
                    name->{showRobotName(name);nameStatus.setText("✅ Zapisano imię: "+name);saveRobotName.setEnabled(true);},
                    error->{nameStatus.setText("Nie udało się odczytać imienia: "+error);saveRobotName.setEnabled(true);}),
                error->{nameStatus.setText("❌ "+error);saveRobotName.setEnabled(true);});
        });
        connect.setOnClickListener(v -> {
            String address=ip.getText().toString().trim();
            String code=pin.getText().toString().trim();
            if (!validPrivateIp(address) || !code.matches("[0-9]{8}")) {
                connectStatus.setText("Wpisz poprawny prywatny adres IP i 8-cyfrowy kod.");
                return;
            }
            prefs.edit().putString("ip",address).putString("pin",code).apply();
            connectStatus.setText("Łączenie...");
            call("/api/status","GET",null,
                 result -> call("/api/name","GET",null,
                     name->{showRobotName(name);connectStatus.setText("✅ Połączono z robotem "+name+".");},
                     error -> connectStatus.setText("❌ Nie odczytano imienia: "+error)),
                 error -> connectStatus.setText("❌ "+error));
        });

        LinearLayout faces=card(root);
        faces.addView(text("🎭 Emocje Ali",20,LIGHT,true));
        faces.addView(text("Po dotknięciu emocja pojawi się na Ali po 5 sekundach.",14,MUTED,false));
        sticky = new CheckBox(this);
        sticky.setText("Na stałe — aż nacisnę X");
        sticky.setTextColor(LIGHT);
        sticky.setButtonTintList(ColorStateList.valueOf(ACCENT));
        sticky.setChecked(true);
        faces.addView(sticky);

        GridLayout grid=new GridLayout(this);
        grid.setColumnCount(4);
        LinearLayout.LayoutParams gridParams = new LinearLayout.LayoutParams(-1,-2);
        gridParams.topMargin=dp(12);
        faces.addView(grid,gridParams);
        for(int i=0;i<moods.length;i++) {
            final String mood=moods[i], label=titles[i];
            Button button=action(label,false);
            button.setTextSize(12);
            button.setAllCaps(false);
            button.setMinHeight(dp(65));
            GridLayout.LayoutParams lp=new GridLayout.LayoutParams();
            lp.width=0;
            lp.height=dp(72);
            lp.columnSpec=GridLayout.spec(i%4,1f);
            lp.rowSpec=GridLayout.spec(i/4);
            lp.setMargins(dp(2),dp(2),dp(2),dp(2));
            grid.addView(button,lp);
            button.setOnClickListener(v -> {
                moodStatus.setText("Wysyłam: "+label);
                call("/api/emotion","POST",mood+"|"+(sticky.isChecked()?"1":"0"),
                     result -> moodStatus.setText("✅ "+label+" — pojawi się za 5 sekund."),
                     error -> moodStatus.setText("❌ "+error));
            });
        }
        Button reset=action("✕ Wyłącz emocję",false);
        faces.addView(reset);
        moodStatus=text("Wybierz emocję.",14,ACCENT,false);
        faces.addView(moodStatus);
        reset.setOnClickListener(v -> call("/api/emotion","POST","reset",
                result -> moodStatus.setText("Emocja wyłączona."),
                error -> moodStatus.setText("❌ "+error)));

        LinearLayout speech=card(root);
        speech.addView(text("✍️ Co Ala ma powiedzieć?",20,LIGHT,true));
        speech.addView(text("Wpisz tekst. Możesz poprawić jego zapis, poprosić Alę o wypowiedzenie go albo zapytać Xiaozhi o odpowiedź.",14,MUTED,false));
        final TextView typedPreview = text("Napisane: (jeszcze nic)",16,LIGHT,false);
        speech.addView(typedPreview);
        sentence=edit("Np. czesc wszystkim, jak sie macie", "",InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE|InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        sentence.setMinLines(3);
        sentence.setMaxLines(7);
        sentence.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE|InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        sentence.setFilters(new InputFilter[]{new InputFilter.LengthFilter(220)});
        sentence.setTextSize(17);
        speech.addView(sentence);
        sentence.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s,int start,int count,int after) {}
            public void onTextChanged(CharSequence s,int start,int before,int count) {
                typedPreview.setText(s.length()==0 ? "Napisane: (jeszcze nic)" : "Napisane: "+s);
            }
            public void afterTextChanged(Editable e) {}
        });
        // When the IME opens, move the editable area into the resized viewport;
        // the previous long emotions grid left it hidden under the keyboard.
        sentence.setOnFocusChangeListener((v,focused)->{
            if(focused)main.postDelayed(()->sentence.requestRectangleOnScreen(
                new Rect(0,0,sentence.getWidth(),sentence.getHeight()+dp(28)),false),300);
        });
        Button correct=action("✨ Wstępnie popraw zdanie",false);
        speech.addView(correct);
        correct.setOnClickListener(v -> sentence.setText(normalize(sentence.getText().toString())));
        speakButton=action("🔊 Niech Ala powie",true);
        speech.addView(speakButton);
        final Button chatButton = action("💬 Zapytaj Alę (Xiaozhi)",true);
        speech.addView(chatButton);
        speechStatus=text("Ala musi być połączona i gotowa do rozmowy.",14,ACCENT,false);
        speech.addView(speechStatus);
        speech.addView(text("Odpowiedź Ali",16,LIGHT,true));
        reply=text("Jeszcze nic nie powiedziała.",15,MUTED,false);
        speech.addView(reply);
        chatButton.setOnClickListener(v -> {
            final boolean hello=nextHello;
            final String textToSend=hello ? "Cześć." : normalize(sentence.getText().toString());
            if (!hello && textToSend.isEmpty()) {
                speechStatus.setText("Wpisz pytanie dla Ali.");return;
            }
            if(textToSend.indexOf((char)34)>=0 || textToSend.indexOf((char)92)>=0 ||
               textToSend.indexOf((char)10)>=0 || textToSend.indexOf((char)13)>=0 ||
               textToSend.getBytes(StandardCharsets.UTF_8).length>400) {
                speechStatus.setText("Skróć tekst i usuń cudzysłowy oraz nowe linie.");return;
            }
            if(!hello)sentence.setText(textToSend);
            typedPreview.setText("Wysłano do Xiaozhi: "+textToSend);
            sentence.clearFocus();
            InputMethodManager keyboard=(InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if(keyboard!=null)keyboard.hideSoftInputFromWindow(sentence.getWindowToken(),0);
            polling=false;pollCount=0;
            speakButton.setEnabled(false);chatButton.setEnabled(false);
            speechStatus.setText(hello ? "Proszę Alę o samo Cześć..." : "Pytam Xiaozhi o odpowiedź...");
            reply.setText("Czekam na odpowiedź Ali...");
            call("/api/chat","POST",hello?"hello":"ask|"+textToSend,
                result -> {
                    nextHello=!hello;
                    chatButton.setText(nextHello ? "👋 Powiedz tylko: Cześć" : "💬 Zapytaj Alę (Xiaozhi)");
                    speakButton.setEnabled(true);chatButton.setEnabled(true);
                    speechStatus.setText(hello ? "✅ Wysłano prośbę o powitanie." : "✅ Pytanie wysłane do Xiaozhi.");
                    polling=true;pollReply();
                    scroll.post(()->scroll.smoothScrollTo(0,speech.getTop()+reply.getTop()));
                },
                error -> {
                    speakButton.setEnabled(true);chatButton.setEnabled(true);
                    speechStatus.setText("❌ "+error);
                    reply.setText("Nie udało się wysłać wiadomości.");
                });
        });
        speakButton.setOnClickListener(v -> {
            String content=normalize(sentence.getText().toString());
            if (content.isEmpty()) {speechStatus.setText("Wpisz najpierw zdanie.");return;}
            if (content.contains("\"") || content.contains("\\") || content.contains("\n") || content.contains("\r")) {
                speechStatus.setText("Usuń cudzysłowy, ukośniki i znaki nowej linii.");return;
            }
            if(content.getBytes(StandardCharsets.UTF_8).length>400) {speechStatus.setText("Skróć zdanie.");return;}
            sentence.setText(content);
            polling=false;
            pollCount=0;
            speakButton.setEnabled(false);
            chatButton.setEnabled(false);
            typedPreview.setText("Wysłano do Xiaozhi: "+content);
            sentence.clearFocus();
            InputMethodManager keyboard=(InputMethodManager)getSystemService(INPUT_METHOD_SERVICE);
            if(keyboard!=null)keyboard.hideSoftInputFromWindow(sentence.getWindowToken(),0);
            speechStatus.setText("Wysyłam zdanie...");
            reply.setText("Oczekuję na odpowiedź Ali...");
            call("/api/say","POST",content,
                result -> {
                    speakButton.setEnabled(true);chatButton.setEnabled(true);
                    speechStatus.setText("✅ Wysłano. Xiaozhi przygotowuje wypowiedź.");
                    polling=true;
                    pollReply();
                },
                error -> {
                    speakButton.setEnabled(true);chatButton.setEnabled(true);
                    speechStatus.setText("❌ "+error);
                    reply.setText("Nie wysłano zdania.");
                });
        });
        // Put writing before the 16 large emotion buttons on compact phones.
        root.removeView(speech);
        root.addView(speech,root.indexOfChild(faces));
        root.addView(text("Pilot lokalny • wymagane Wi-Fi i firmware Ali z obsługą panelu. Połączenie HTTP nie jest szyfrowane — używaj prywatnej sieci.",12,MUTED,false));
    }

    private void showRobotName(String value) {
        titleView.setText("💙 Pilot: "+value);
        robotName.setText(value);
        nameStatus.setText("Aktualne imię: "+value);
    }
    private static boolean validRobotName(String name) {
        if (!name.matches("[A-Za-zĄąĆćĘęŁłŃńÓóŚśŹźŻż]+([ -][A-Za-zĄąĆćĘęŁłŃńÓóŚśŹźŻż]+)*"))
            return false;
        int letters=0;
        for(int i=0;i<name.length();i++)if(Character.isLetter(name.charAt(i)))letters++;
        return letters>=2 && letters<=24 && name.getBytes(StandardCharsets.UTF_8).length<=48;
    }
    private static boolean validPrivateIp(String value) {
        if(!value.matches("(?:[0-9]{1,3}\\.){3}[0-9]{1,3}"))return false;
        String[] parts=value.split("\\.");
        int[] p=new int[4];
        try {for(int i=0;i<4;i++){p[i]=Integer.parseInt(parts[i]);if(p[i]<0||p[i]>255)return false;}}
        catch(NumberFormatException e){return false;}
        return p[0]==10 || (p[0]==172&&p[1]>=16&&p[1]<=31) || (p[0]==192&&p[1]==168);
    }
    private static String normalize(String value) {
        String s=value.trim().replaceAll("\\s+"," ").replaceAll("\\s+([.,!?;:])","$1")
                .replaceAll("([.,!?;:])(?=\\S)","$1 ");
        if(s.isEmpty())return s;
        s=s.substring(0,1).toUpperCase(java.util.Locale.forLanguageTag("pl-PL"))+s.substring(1);
        if (!s.matches("(?s).*[.!?…]$"))s+=".";
        return s;
    }
    private int dp(int value){return (int)(value*getResources().getDisplayMetrics().density+0.5f);}
    private TextView text(String value,int size,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(value);t.setTextColor(color);t.setTextSize(size);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        t.setPadding(0,dp(8),0,dp(8));
        return t;
    }
    private LinearLayout card(LinearLayout root){
        LinearLayout layout=new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(dp(16),dp(14),dp(16),dp(16));
        android.graphics.drawable.GradientDrawable shape=new android.graphics.drawable.GradientDrawable();
        shape.setColor(CARD);shape.setCornerRadius(dp(18));shape.setStroke(dp(1),Color.rgb(42,65,82));
        layout.setBackground(shape);
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,-2);
        params.topMargin=dp(17);root.addView(layout,params);
        return layout;
    }
    private EditText edit(String hint,String value,int type){
        EditText e=new EditText(this);
        e.setSingleLine((type & InputType.TYPE_TEXT_FLAG_MULTI_LINE)==0);
        e.setHint(hint);e.setText(value);e.setTextColor(LIGHT);e.setHintTextColor(MUTED);
        e.setTextSize(15);e.setInputType(type);
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();
        bg.setColor(Color.rgb(11,24,40));bg.setCornerRadius(dp(10));
        bg.setStroke(dp(1),Color.rgb(57,82,104));
        e.setBackground(bg);e.setPadding(dp(12),dp(12),dp(12),dp(12));
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,-2);
        params.topMargin=dp(7);e.setLayoutParams(params);
        return e;
    }
    private Button action(String label,boolean primary){
        Button b=new Button(this);
        b.setText(label);b.setAllCaps(false);b.setTextSize(15);
        b.setTextColor(primary?BG:LIGHT);
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();
        bg.setColor(primary?ACCENT:BUTTON_BG);
        bg.setCornerRadius(dp(12));b.setBackground(bg);
        b.setPadding(dp(6),0,dp(6),0);
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,dp(52));
        params.topMargin=dp(10);b.setLayoutParams(params);
        return b;
    }
    private interface Result { void accept(String value); }
    private void call(String endpoint,String method,String body,Result done,Result fail){
        final String address=prefs.getString("ip",""), code=prefs.getString("pin","");
        if(!validPrivateIp(address)||!code.matches("[0-9]{8}")) {
            fail.accept("Najpierw wpisz adres IP i kod, a potem kliknij Połącz.");return;
        }
        io.execute(()->{
            HttpURLConnection c=null;
            try {
                c=(HttpURLConnection)new URL("http://"+address+endpoint).openConnection();
                c.setRequestMethod(method);c.setConnectTimeout(6000);c.setReadTimeout(8000);
                c.setUseCaches(false);c.setRequestProperty("X-Ala-Pin",code);
                if(body!=null){
                    c.setDoOutput(true);
                    c.setRequestProperty("Content-Type","text/plain; charset=UTF-8");
                    byte[] data=body.getBytes(StandardCharsets.UTF_8);
                    try(OutputStream out=c.getOutputStream()){out.write(data);}
                }
                int status=c.getResponseCode();
                InputStream input=status<400?c.getInputStream():c.getErrorStream();
                String message="";
                if(input!=null){try(InputStream src=input){message=new String(src.readAllBytes(),StandardCharsets.UTF_8);}}
                final String result=message.trim();
                if(status>=200 && status<300)main.post(()->done.accept(result));
                else main.post(()->fail.accept(status==401?"Błędny kod.":(result.isEmpty()?"Błąd HTTP "+status:result)));
            }catch(Exception ex){
                main.post(()->fail.accept("Nie mogę połączyć z Alą. Sprawdź Wi-Fi, adres IP i firmware."));
            }finally {if(c!=null)c.disconnect();}
        });
    }
    private void pollReply(){
        if(!polling || isFinishing())return;
        if(++pollCount>35){polling=false;speechStatus.setText("Nie otrzymano odpowiedzi. Sprawdź Xiaozhi.");return;}
        main.postDelayed(()->call("/api/reply","GET",null,
            result->{if(!polling)return;if(!result.isEmpty()){
                  polling=false;reply.setText(result);speechStatus.setText("✅ Ala otrzymała tekst odpowiedzi.");
                }else pollReply();},
            error->{polling=false;speechStatus.setText("Błąd: "+error);}),1000);
    }
    @Override protected void onDestroy(){polling=false;io.shutdownNow();super.onDestroy();}
}
