package pl.homemind.espenmobile;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.text.Normalizer;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Locale;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Samodzielna aplikacja demonstracyjna Espen na Androida.
 * Nie podszywa się pod urządzenie Xiaozhi ani nie używa tokenów z firmware.
 */
public class MainActivity extends Activity {
    private static final int MIC_REQUEST = 42;
    private static final Locale POLISH = new Locale("pl", "PL");
    private static final int IDLE = 0, LISTENING = 1, THINKING = 2, SPEAKING = 3, SLEEPING = 4;

    private FaceView face;
    private TextView status;
    private TextView liveText;
    private TextView conversation;
    private ScrollView conversationScroll;
    private Button talkButton;
    private Button quietButton;
    private SpeechRecognizer recognizer;
    private TextToSpeech tts;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean muted = false;
    private boolean listening = false;
    private boolean thinking = false;
    private boolean speaking = false;
    private boolean ttsReady = false;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(10, 15, 32));
        getWindow().setNavigationBarColor(Color.rgb(10, 15, 32));
        buildUi();
        initTts();
        setState();
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView label(String value, int size, int color) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        return t;
    }

    private Button button(String value, int color) {
        Button b = new Button(this);
        b.setText(value);
        b.setAllCaps(false);
        b.setTextSize(15);
        b.setTextColor(Color.WHITE);
        android.graphics.drawable.GradientDrawable bg = new android.graphics.drawable.GradientDrawable();
        bg.setColor(color);
        bg.setCornerRadius(dp(20));
        b.setBackground(bg);
        return b;
    }

    private void buildUi() {
        ScrollView outer = new ScrollView(this);
        outer.setFillViewport(true);
        outer.setBackgroundColor(Color.rgb(10, 15, 32));
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(dp(16), dp(18), dp(16), dp(24));
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        outer.addView(column);

        TextView title = label("E S P E N   /   M O B I L E", 16, Color.rgb(112, 231, 241));
        title.setPadding(0, dp(10), 0, dp(8));
        column.addView(title, new LinearLayout.LayoutParams(-1, dp(48)));

        TextView subtitle = label("Twój mały, rozmowny robot", 13, Color.rgb(153, 171, 203));
        column.addView(subtitle);

        face = new FaceView(this);
        face.setContentDescription("Animowana buźka Espena; dotknij, aby mówić, przesuń w dół, aby uśpić");
        LinearLayout.LayoutParams faceParams = new LinearLayout.LayoutParams(-1, dp(306));
        faceParams.topMargin = dp(8);
        column.addView(face, faceParams);
        face.setOnTouchListener(new View.OnTouchListener() {
            private float downY;
            @Override public boolean onTouch(View v, MotionEvent event) {
                if (event.getAction() == MotionEvent.ACTION_DOWN) {
                    downY = event.getY();
                    return true;
                }
                if (event.getAction() == MotionEvent.ACTION_UP) {
                    if (event.getY() - downY > dp(70)) {
                        goToSleep();
                    } else {
                        onTalkPressed();
                    }
                    v.performClick();
                    return true;
                }
                return true;
            }
        });

        status = label("", 20, Color.WHITE);
        status.setPadding(0, dp(6), 0, dp(2));
        column.addView(status, new LinearLayout.LayoutParams(-1, dp(36)));

        liveText = label("Dotknij buźki lub przycisku Mów", 13, Color.rgb(153, 171, 203));
        liveText.setPadding(dp(12), 0, dp(12), dp(8));
        column.addView(liveText);

        conversationScroll = new ScrollView(this);
        conversationScroll.setBackgroundColor(Color.rgb(19, 28, 51));
        conversation = label("Espen: Cześć! Powiedz coś do mnie. 😄", 14, Color.rgb(233, 241, 255));
        conversation.setGravity(Gravity.TOP | Gravity.LEFT);
        conversation.setPadding(dp(15), dp(13), dp(15), dp(13));
        conversationScroll.addView(conversation);
        LinearLayout.LayoutParams logParams = new LinearLayout.LayoutParams(-1, dp(115));
        logParams.bottomMargin = dp(12);
        column.addView(conversationScroll, logParams);

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        controls.setGravity(Gravity.CENTER);
        talkButton = button("🎙  Mów", Color.rgb(26, 134, 151));
        quietButton = button("☾  Bez odbioru", Color.rgb(60, 71, 100));
        LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(0, dp(58), 1);
        left.rightMargin = dp(8);
        controls.addView(talkButton, left);
        controls.addView(quietButton, new LinearLayout.LayoutParams(0, dp(58), 1));
        column.addView(controls, new LinearLayout.LayoutParams(-1, -2));
        talkButton.setOnClickListener(v -> onTalkPressed());
        quietButton.setOnClickListener(v -> {
            if (muted) wakeUp();
            else goToSleep();
        });

        TextView note = label("Wersja 0.1 • głos systemowy Android • bez połączenia z Xiaozhi", 11, Color.rgb(121, 142, 174));
        note.setPadding(dp(6), dp(18), dp(6), 0);
        column.addView(note, new LinearLayout.LayoutParams(-1, -2));
        setContentView(outer);
    }

    private void initTts() {
        tts = new TextToSpeech(this, result -> {
            if (result != TextToSpeech.SUCCESS || tts == null) {
                ttsReady = false;
                return;
            }
            int language = tts.setLanguage(POLISH);
            ttsReady = language != TextToSpeech.LANG_MISSING_DATA &&
                       language != TextToSpeech.LANG_NOT_SUPPORTED;
            tts.setSpeechRate(1.04f);
            tts.setPitch(1.08f);
            tts.setOnUtteranceProgressListener(new UtteranceProgressListener() {
                @Override public void onStart(String id) {
                    ui.post(() -> { speaking = true; thinking = false; setState(); });
                }
                @Override public void onDone(String id) {
                    ui.post(() -> { speaking = false; setState(); });
                }
                @Override public void onError(String id) {
                    ui.post(() -> { speaking = false; setState(); });
                }
            });
        });
    }

    private void onTalkPressed() {
        if (muted) {
            wakeUp();
            return;
        }
        if (listening || speaking || thinking) {
            interrupt();
            return;
        }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, MIC_REQUEST);
            return;
        }
        startListening();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == MIC_REQUEST) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) {
                startListening();
            } else {
                liveText.setText("Pozwól na użycie mikrofonu w ustawieniach Androida.");
            }
        }
    }

    private void initRecognizer() {
        if (recognizer != null) return;
        if (!SpeechRecognizer.isRecognitionAvailable(this)) {
            liveText.setText("Brak systemowej usługi rozpoznawania mowy.");
            return;
        }
        recognizer = SpeechRecognizer.createSpeechRecognizer(this);
        recognizer.setRecognitionListener(new RecognitionListener() {
            @Override public void onReadyForSpeech(Bundle params) {
                liveText.setText("Słucham... mów śmiało!");
                setState();
            }
            @Override public void onBeginningOfSpeech() {
                liveText.setText("Słyszę Cię!");
            }
            @Override public void onRmsChanged(float rmsdB) {
                face.setMicLevel(Math.max(0f, Math.min(1f, rmsdB / 12f)));
            }
            @Override public void onBufferReceived(byte[] buffer) {}
            @Override public void onEndOfSpeech() {
                if (listening) {
                    thinking = true;
                    setState();
                    liveText.setText("Rozpoznaję słowa...");
                }
            }
            @Override public void onError(int error) {
                if (!listening) return;
                listening = false;
                thinking = false;
                face.setMicLevel(0);
                if (error == SpeechRecognizer.ERROR_NO_MATCH ||
                    error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT) {
                    liveText.setText("Nie udało mi się usłyszeć słów. Spróbuj jeszcze raz.");
                } else {
                    liveText.setText("Mikrofon jest niedostępny (kod " + error + "). Spróbuj ponownie.");
                }
                setState();
            }
            @Override public void onResults(Bundle results) {
                if (!listening) return;
                listening = false;
                thinking = false;
                face.setMicLevel(0);
                ArrayList<String> options = results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                if (options == null || options.isEmpty()) {
                    liveText.setText("Nie rozpoznałem wypowiedzi.");
                    setState();
                    return;
                }
                handleRecognized(options.get(0));
            }
            @Override public void onPartialResults(Bundle results) {
                ArrayList<String> options = results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                if (listening && options != null && !options.isEmpty()) {
                    liveText.setText("Słyszę: " + options.get(0));
                }
            }
            @Override public void onEvent(int eventType, Bundle params) {}
        });
    }

    private void startListening() {
        if (muted || listening) return;
        initRecognizer();
        if (recognizer == null) return;
        listening = true;
        thinking = false;
        speaking = false;
        setState();
        liveText.setText("Uruchamiam mikrofon...");
        Intent request = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        request.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        request.putExtra(RecognizerIntent.EXTRA_LANGUAGE, "pl-PL");
        request.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        request.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        try {
            recognizer.startListening(request);
        } catch (RuntimeException ex) {
            listening = false;
            liveText.setText("Nie mogę uruchomić rozpoznawania mowy.");
            setState();
        }
    }

    private void handleRecognized(String words) {
        liveText.setText("Rozpoznano: " + words);
        appendConversation("Ty", words);
        String normalized = ReplyEngine.normalize(words);
        if (normalized.contains("bez odbioru") || normalized.contains("dobranoc")) {
            appendConversation("Espen", "Bez odbioru. Dotknij mnie, żeby mnie obudzić.");
            goToSleep();
            return;
        }
        String reply = ReplyEngine.answer(words);
        appendConversation("Espen", reply);
        say(reply);
    }

    private void say(String words) {
        if (muted) return;
        if (!ttsReady || tts == null) {
            liveText.setText("Odpowiedź jest na ekranie. Zainstaluj polski głos TTS, żeby ją usłyszeć.");
            setState();
            return;
        }
        speaking = true;
        setState();
        int code = tts.speak(words, TextToSpeech.QUEUE_FLUSH, null, "espen_" + SystemClock.uptimeMillis());
        if (code == TextToSpeech.ERROR) {
            speaking = false;
            liveText.setText("Nie udało się odtworzyć głosu.");
            setState();
        }
    }

    private void appendConversation(String who, String message) {
        String next = conversation.getText() + "\n\n" + who + ": " + message;
        if (next.length() > 5000) next = next.substring(next.length() - 4500);
        conversation.setText(next);
        conversationScroll.post(() -> conversationScroll.fullScroll(View.FOCUS_DOWN));
    }

    private void interrupt() {
        listening = false;
        thinking = false;
        speaking = false;
        if (recognizer != null) recognizer.cancel();
        if (tts != null) tts.stop();
        face.setMicLevel(0);
        setState();
    }

    private void goToSleep() {
        interrupt();
        muted = true;
        liveText.setText("Bez odbioru. Dotknij buźki, aby wybudzić Espena.");
        setState();
    }

    private void wakeUp() {
        muted = false;
        liveText.setText("Jestem! Dotknij ponownie, żeby mówić.");
        setState();
    }

    private void setState() {
        if (face == null) return;
        int mode = muted ? SLEEPING : listening ? LISTENING : thinking ? THINKING : speaking ? SPEAKING : IDLE;
        face.setMood(mode);
        status.setText(mode == SLEEPING ? "Śpię 💤" :
                mode == LISTENING ? "Słucham 👂" :
                mode == THINKING ? "Myślę..." :
                mode == SPEAKING ? "Odpowiadam 😄" : "Czekam na Ciebie ✨");
        talkButton.setText(mode == SLEEPING ? "☀  Obudź" :
                mode == LISTENING || mode == SPEAKING || mode == THINKING ? "■  Przerwij" : "🎙  Mów");
        quietButton.setText(muted ? "☀  Obudź" : "☾  Bez odbioru");
    }

    @Override public void onStop() {
        super.onStop();
        interrupt();
    }

    @Override public void onDestroy() {
        if (recognizer != null) {
            recognizer.destroy();
            recognizer = null;
        }
        if (tts != null) {
            tts.stop();
            tts.shutdown();
            tts = null;
        }
        super.onDestroy();
    }

    private static final class ReplyEngine {
        private static final Random RANDOM = new Random();
        private static final Pattern MATH = Pattern.compile(
                "(\\d{1,5})\\s*(plus|dodac|\\+|minus|odjac|\\-|razy|pomnozyc|x|\\*)\\s*(\\d{1,5})");
        static String normalize(String s) {
            return Normalizer.normalize(s.toLowerCase(POLISH), Normalizer.Form.NFD)
                    .replaceAll("\\p{M}+", "").replace('ł', 'l');
        }
        static String answer(String input) {
            String s = normalize(input);
            Matcher math = MATH.matcher(s);
            if (math.find()) {
                long a = Long.parseLong(math.group(1));
                long b = Long.parseLong(math.group(3));
                String op = math.group(2);
                long result = (op.equals("minus") || op.equals("odjac") || op.equals("-")) ? a - b :
                        (op.equals("razy") || op.equals("pomnozyc") || op.equals("x") || op.equals("*")) ? a * b : a + b;
                return "To " + result + ". A teraz przyznaj, kto tu jest kalkulatorem z osobowością?";
            }
            if (s.contains("godzin") || s.contains("ktora jest")) {
                return "Jest " + new SimpleDateFormat("HH:mm", POLISH).format(new Date()) + ". Czas na małą przygodę!";
            }
            if (s.contains("zart") || s.contains("dowcip") || s.contains("rozsmiesz")) {
                String[] jokes = {
                        "Dlaczego komputer nie poszedł na spacer? Bo bał się złapać wirusa!",
                        "Co mówi zero do ósemki? Fajny pasek!",
                        "Dlaczego robot nie lubi schodów? Bo woli iść krok po kroku... w kodzie!",
                        "Jak nazywa się robot, który opowiada żarty? Komediant binarny!"
                };
                return jokes[RANDOM.nextInt(jokes.length)];
            }
            if (s.contains("jak masz na imie") || s.contains("jak sie nazywasz") || s.contains("kim jestes")) {
                return "Jestem Espen. Mały robot z wielką buzią i jeszcze większym apetytem na rozmowy!";
            }
            if (s.contains("jak sie czujesz") || s.contains("co slychac")) {
                return "Moje obwody są w świetnym humorze. Jeszcze tylko nie nauczyłem się robić herbaty!";
            }
            if (s.contains("czesc") || s.contains("hej") || s.contains("dzien dobry") || s.contains("witaj")) {
                return "Cześć! Tu Espen. Jestem gotowy na żarty, pytania i matematyczne pojedynki!";
            }
            if (s.contains("dziekuje") || s.contains("dzieki")) {
                return "Cała przyjemność po mojej stronie!";
            }
            if (s.contains("co potrafisz") || s.contains("co umiesz") || s.contains("pomoc")) {
                return "Potrafię słuchać, mówić, liczyć, sprawdzać godzinę i opowiadać żarty. Prawdziwe rozmowy AI przez Xiaozhi dodamy w następnej wersji.";
            }
            if (s.contains("co widzisz") || s.contains("zrob zdjecie") || s.contains("aparat")) {
                return "Na razie nie mam włączonej analizy aparatu. Mogę za to słuchać i odpowiadać.";
            }
            if (s.contains("xiaozhi") || s.contains("sztuczna inteligencja")) {
                return "Jestem wersją demonstracyjną na Androida. Połączenie z Xiaozhi jeszcze powstaje.";
            }
            return "Słyszałem: " + input + ". Na razie znam kilka sprytnych odpowiedzi. Poproś mnie o żart, zapytaj o godzinę albo policz coś ze mną!";
        }
    }

    private static final class FaceView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int mood = IDLE;
        private float micLevel = 0f;
        private final RectF rect = new RectF();

        FaceView(Activity activity) {
            super(activity);
            setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        }
        void setMood(int next) {
            mood = next;
            invalidate();
        }
        void setMicLevel(float value) {
            micLevel = value;
            invalidate();
        }
        private void fill(Canvas c, int color, float left, float top, float right, float bottom, float r) {
            p.setShader(null);
            p.setStyle(Paint.Style.FILL);
            p.setColor(color);
            rect.set(left, top, right, bottom);
            c.drawRoundRect(rect, r, r, p);
        }
        @Override protected void onDraw(Canvas actual) {
            super.onDraw(actual);
            int save = actual.save();
            float scale = Math.min(getWidth() / 400f, getHeight() / 306f);
            actual.translate((getWidth() - 400f * scale) / 2f,
                    (getHeight() - 306f * scale) / 2f);
            actual.scale(scale, scale);

            boolean sleepy = mood == SLEEPING;
            int accent = sleepy ? Color.rgb(100, 111, 142) :
                    mood == LISTENING ? Color.rgb(85, 255, 200) :
                    mood == SPEAKING ? Color.rgb(255, 186, 109) : Color.rgb(99, 215, 247);
            p.setStyle(Paint.Style.FILL);
            p.setShader(new LinearGradient(52, 4, 354, 295,
                    Color.rgb(27, 47, 82), Color.rgb(13, 25, 53), Shader.TileMode.CLAMP));
            rect.set(10, 8, 390, 298);
            actual.drawRoundRect(rect, 88, 88, p);
            p.setShader(null);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(4);
            p.setColor(accent);
            p.setAlpha(180);
            actual.drawRoundRect(rect, 88, 88, p);
            p.setStyle(Paint.Style.FILL);
            p.setAlpha(255);
            fill(actual, Color.rgb(35, 57, 96), 24, 22, 376, 284, 75);

            long time = SystemClock.uptimeMillis();
            boolean blink = !sleepy && (time % 4600L) < 145L;
            float wobble = (float) Math.sin(time / 320.0) * 4f;
            float eyeH = sleepy || blink ? 7 : 78;
            int eyes = sleepy ? Color.rgb(134, 150, 180) : accent;
            fill(actual, eyes, 89, 85 + (78 - eyeH) / 2f, 162, 85 + (78 + eyeH) / 2f, 30);
            fill(actual, eyes, 238, 85 + (78 - eyeH) / 2f, 311, 85 + (78 + eyeH) / 2f, 30);
            if (!sleepy && !blink) {
                float offset = mood == LISTENING ? micLevel * 5 : wobble;
                fill(actual, Color.rgb(15, 25, 52), 118 + offset, 117, 141 + offset, 143, 11);
                fill(actual, Color.rgb(15, 25, 52), 267 + offset, 117, 290 + offset, 143, 11);
                fill(actual, Color.WHITE, 121 + offset, 117, 127 + offset, 125, 4);
                fill(actual, Color.WHITE, 270 + offset, 117, 276 + offset, 125, 4);
            }

            fill(actual, Color.rgb(211, 99, 125), 55, 188, 86, 208, 12);
            fill(actual, Color.rgb(211, 99, 125), 314, 188, 345, 208, 12);
            if (sleepy) {
                fill(actual, eyes, 168, 218, 232, 224, 7);
            } else if (mood == SPEAKING) {
                float mouthH = 30 + (float) Math.abs(Math.sin(time / 120.0)) * 37;
                fill(actual, Color.rgb(7, 19, 35), 169, 205, 231, 205 + mouthH, 29);
                fill(actual, Color.rgb(255, 139, 158), 182, 225 + mouthH / 5f, 219, 230 + mouthH / 3f, 12);
            } else if (mood == LISTENING) {
                float size = 19 + micLevel * 16;
                fill(actual, Color.rgb(8, 20, 40), 200 - size, 220 - size, 200 + size, 220 + size, size);
            } else if (mood == THINKING) {
                fill(actual, accent, 174, 218, 186, 232, 9);
                fill(actual, accent, 194, 218, 206, 232, 9);
                fill(actual, accent, 214, 218, 226, 232, 9);
            } else {
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeCap(Paint.Cap.ROUND);
                p.setStrokeWidth(12);
                p.setColor(accent);
                Path smile = new Path();
                smile.moveTo(160, 210);
                smile.quadTo(200, 263, 240, 210);
                actual.drawPath(smile, p);
                p.setStyle(Paint.Style.FILL);
            }
            if (mood == LISTENING || mood == SPEAKING || mood == THINKING) {
                postInvalidateDelayed(110);
            } else if (!sleepy) {
                postInvalidateDelayed(230);
            }
            actual.restoreToCount(save);
        }
    }
}
