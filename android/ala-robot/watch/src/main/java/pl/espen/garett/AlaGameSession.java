package pl.espen.garett;

import java.util.ArrayDeque;
import java.util.Random;

/** Game and foreground story decisions shared by Android phone and Garett. */
final class AlaGameSession {
    interface Ui {
        void options(String title,String[] choices);
        void sayLocally(String message,String[] choices);
        boolean playButton(String clip,String context);
        void listenForIdea();
        void sayIdeaPrompt(String message);
        void stopSpeech();
        void status(String message);
        void reaction(AlaBehavior.Mood mood);
        void highlight(int index);
    }

    private enum Phase { OFF, MENU, THEMES, QUIZ, ANIMALS, END, INTRO, CHAPTER, CHOICE, IDEA, ERROR }
    private enum Decision { START, ACCEPT, IDEA, REPEAT, END }
    private static final String[] MENU={"Quiz Mistrza","Zgadnij zwierzę","Magiczna opowieść"};
    private static final String[] THEMES={"Magiczny las","Nad jeziorem","Z mamą w samochodzie"};
    private static final String[] CLIPS={"forest","lake","car"};
    private static final String[] CHOICES={"Podoba mi się","Mam inny pomysł","Koniec opowieści"};
    private static final String[] IDEA={"Powiedz do mikrofonu","Wróć do pomysłów","Zamknij"};
    private static final int ROUNDS=5;

    // Question order and scoring policy from firmware/ala_play_games.h, c5e0040.
    private static final String[][] QUIZ={
        {"Ile to 7 + 8?","15","14","16","0"},
        {"Która planeta jest najbliżej Słońca?","Mars","Merkury","Neptun","1"},
        {"Ile nóg ma pająk?","6","8","10","1"},
        {"Co rośnie z nasionka?","Roślina","Kamień","Chmura","0"},
        {"Które zwierzę jest ssakiem?","Żaba","Delfin","Rekin","1"},
        {"Ile minut ma godzina?","100","30","60","2"},
        {"Jaki kolor powstanie z niebieskiego i żółtego?","Zielony","Czerwony","Biały","0"},
        {"Który kontynent jest największy?","Europa","Azja","Australia","1"},
        {"Ile boków ma trójkąt?","4","6","3","2"},
        {"Co przyciąga magnes?","Żelazo","Drewno","Papier","0"},
        {"Jaki narząd pompuje krew?","Płuca","Serce","Żołądek","1"},
        {"Ile to 9 razy 3?","18","24","27","2"}
    };
    private static final String[][] ANIMALS={
        {"Mam długą szyję i jem liście z drzew. Kim jestem?","Żyrafa","Lis","Pingwin","0"},
        {"Mam czarno-białe pasy i mieszkam w Afryce.","Koń","Zebra","Tygrys","1"},
        {"Skaczę, mam torbę na brzuchu i mieszkam w Australii.","Kangur","Niedźwiedź","Koala","0"},
        {"Jestem ogromny, szary i mam trąbę.","Słoń","Hipopotam","Nosorożec","0"},
        {"Potrafię zmieniać kolor skóry i mam długi język.","Kameleon","Bóbr","Orzeł","0"},
        {"Jestem ptakiem, ale nie latam. Pływam w zimnej wodzie.","Wróbel","Pingwin","Sokół","1"},
        {"Niosę domek na plecach i poruszam się powoli.","Ślimak","Mrówka","Żuk","0"},
        {"Świecę nocą nad łąką jak mała latarenka.","Motyl","Biedronka","Świetlik","2"},
        {"Mam osiem ramion i mieszkam w morzu.","Krab","Ośmiornica","Meduza","1"}
    };

    private final Ui ui;
    private final Random random;
    private final ArrayDeque<String> earlier=new ArrayDeque<>();
    private final StringBuilder speech=new StringBuilder();
    private Phase phase=Phase.OFF,previousGame=Phase.QUIZ;
    private Decision decision=Decision.START;
    private String world="",lastChapter="",idea="";
    private int theme,queuedTheme=-1,seed,round,score;

    AlaGameSession(Ui ui){this(ui,new Random());}
    AlaGameSession(Ui ui,Random random){this.ui=ui;this.random=random;}
    boolean isRunning(){return phase!=Phase.OFF;}
    boolean isNarrating(){return phase==Phase.CHAPTER;}
    boolean wantsIdea(){return phase==Phase.IDEA;}

    void open(){
        if (phase!=Phase.OFF) close();
        phase=Phase.MENU;
        ui.options("Gry Ali",MENU);
        ui.sayLocally("Zagrajmy! Wybierz jedną z gier.",MENU);
    }

    void close(){
        phase=Phase.OFF;
        earlier.clear();
        speech.setLength(0);
        world=lastChapter=idea="";
        queuedTheme=-1;
        ui.stopSpeech();
        ui.options("",null);
    }

    void choose(int index){
        if (index<0 || index>2) return;
        switch (phase) {
            case MENU:
                if (index==2) introduceStory();
                else startQuiz(index==0 ? Phase.QUIZ : Phase.ANIMALS);
                break;
            case INTRO:
                queuedTheme=index;
                break;
            case THEMES:
                theme=index;
                narrate(Decision.START);
                break;
            case QUIZ: case ANIMALS:
                answerQuiz(index);
                break;
            case END:
                if (index==0) startQuiz(previousGame);
                else if (index==1) open();
                else close();
                break;
            case CHOICE:
                if (index==0) narrate(Decision.ACCEPT);
                else if (index==1) promptIdea();
                else narrate(Decision.END);
                break;
            case IDEA:
                if (index==0) ui.listenForIdea();
                else if (index==1) showDecisions();
                else close();
                break;
            case ERROR:
                if (index==0) {
                    if (lastChapter.isEmpty() && queuedTheme==-2) introduceStory();
                    else narrate(decision);
                } else if (index==1) open();
                else close();
                break;
            default: break;
        }
    }

    private void introduceStory(){
        phase=Phase.INTRO;
        queuedTheme=-1;
        ui.options("Temat opowieści",THEMES);
        if (!ui.playButton("intro","")) onNarrationFailed();
    }

    private void startQuiz(Phase mode){
        phase=previousGame=mode;
        round=score=0;
        seed=random.nextInt(108);
        askQuestion("");
    }

    private String[] item(){
        String[][] questions=phase==Phase.QUIZ ? QUIZ : ANIMALS;
        return questions[(seed+round*5)%questions.length];
    }

    private void answerQuiz(int index){
        String[] q=item();
        int correct=Integer.parseInt(q[4]);
        boolean good=index==correct;
        if (good) score+=10;
        ui.reaction(good ? AlaBehavior.Mood.HAPPY : AlaBehavior.Mood.THINKING);
        String feedback=good ? "Brawo! " : "Poprawna odpowiedź to "+q[1+correct]+". ";
        if (++round<ROUNDS) {
            askQuestion(feedback);
        } else {
            phase=Phase.END;
            String[] buttons={"Jeszcze raz","Gry","Zamknij"};
            ui.options("Wynik: "+score+"/50",buttons);
            ui.sayLocally(feedback+"Koniec gry. Twój wynik to "+score+" punktów.",buttons);
        }
    }

    private void askQuestion(String feedback){
        String[] q=item();
        String[] opts={q[1],q[2],q[3]};
        ui.options(q[0],opts);
        ui.sayLocally(feedback+q[0],opts);
    }

    void onPlayedSentence(String text){
        if (phase==Phase.INTRO) {
            String normalized=AlaSocial.words(text);
            if (normalized.contains(" samochod")) ui.highlight(2);
            else if (normalized.contains(" jezior")) ui.highlight(1);
            else if (normalized.contains(" las") || normalized.contains(" lesie")) ui.highlight(0);
            else ui.highlight(-1);
        } else if (phase==Phase.CHAPTER && text!=null && !text.trim().isEmpty() && speech.length()<1500) {
            if (speech.length()>0) speech.append(' ');
            speech.append(bound(text,1500-speech.length()));
        }
    }

    void onTurnFinished(){
        if (phase==Phase.INTRO) {
            ui.highlight(-1);
            phase=Phase.THEMES;
            if (queuedTheme>=0) choose(queuedTheme);
            return;
        }
        if (phase!=Phase.CHAPTER) return;
        if (speech.length()==0) { onNarrationFailed(); return; }
        if (decision==Decision.END) {
            close();
            ui.status("Do zobaczenia w następnej przygodzie!");
            return;
        }
        if (decision!=Decision.REPEAT) {
            if (lastChapter.isEmpty()) world=bound(speech.toString(),500);
            else {
                String[] first=lastChapter.split("[.!?]",2);
                earlier.add(bound(first[0],170));
                if (earlier.size()>5) earlier.removeFirst();
            }
            lastChapter=speech.toString();
        }
        showDecisions();
    }

    private void showDecisions(){
        phase=Phase.CHOICE;
        ui.options("Co dalej?",CHOICES);
    }

    private void promptIdea(){
        phase=Phase.IDEA;
        ui.options("Twój pomysł",IDEA);
        ui.sayIdeaPrompt("Opowiedz mi, co ma się wydarzyć.");
    }

    void onNarrationFailed(){
        if (phase==Phase.INTRO || phase==Phase.CHAPTER) {
            if (phase==Phase.INTRO) queuedTheme=-2;
            phase=Phase.ERROR;
            ui.options("Nie udało się opowiedzieć",new String[]{"Spróbuj ponownie","Wróć do gier","Zamknij"});
        } else if (phase==Phase.IDEA) {
            ui.options("Spróbuj powiedzieć jeszcze raz",IDEA);
        }
    }

    void onVoiceIdea(String spoken){
        if (phase!=Phase.IDEA) return;
        String words=AlaSocial.words(spoken);
        if (words.equals(" ")) { ui.status("Nie rozpoznałam pomysłu. Spróbuj jeszcze raz."); return; }
        if (words.equals(" koniec gry ") || words.equals(" zamknij gre ")) { close(); return; }
        if (words.equals(" tak ") || words.equals(" dobrze ") || words.equals(" twoj pomysl ")
            || words.equals(" zrobmy tak ") || words.equals(" nie wiem ")) narrate(Decision.ACCEPT);
        else if (words.equals(" nie ") || words.equals(" inaczej ") || words.equals(" mam inny pomysl ")) promptIdea();
        else if (words.equals(" powtorz ") || words.equals(" jeszcze raz ")) narrate(Decision.REPEAT);
        else if (words.equals(" zakoncz ") || words.equals(" skonczmy ") || words.equals(" zakonczmy ")) narrate(Decision.END);
        else { idea=bound(spoken,700); narrate(Decision.IDEA); }
    }

    private void narrate(Decision next){
        decision=next;
        phase=Phase.CHAPTER;
        speech.setLength(0);
        ui.options("",null);
        boolean first=lastChapter.isEmpty();
        if (!ui.playButton(first ? CLIPS[theme] : "story_context",first ? "" : continuation()))
            onNarrationFailed();
    }

    private String continuation(){
        String request;
        switch (decision) {
            case ACCEPT: request="Dziecko akceptuje. Zrealizuj ostatnią propozycję Ali zawartą w ostatnim fragmencie."; break;
            case IDEA: request="Pomysł dziecka: «"+idea+"». Rozwiń fabułę zgodnie z nim."; break;
            case REPEAT: request="Powtórz tylko ostatnią propozycję i pytanie. Bez nowego fragmentu."; break;
            case END: request="Opowiedz krótkie zakończenie i pożegnaj dziecko."; break;
            default: throw new IllegalStateException("Rozdział bez decyzji");
        }
        String prompt="Jesteś Alą, przyjazną narratorką Magicznej Opowieści dla dziecka. Mów po polsku, ciepło i naturalnie. "
            +"Temat: "+THEMES[theme]+". Zachowaj bohaterów i ciągłość tej samej przygody.\n"
            +"Początek, bohaterowie i świat: "+world+"\n"
            +"Wcześniejsze wydarzenia: "+String.join("; ",earlier)+"\n"
            +"Ostatni fragment: "+lastChapter+"\n"+request;
        if (decision==Decision.END || decision==Decision.REPEAT) return prompt;
        return prompt+" Opowiedz około 5–8 zdań. Zaproponuj jeden dalszy krok i zapytaj: "
            +"„Podoba ci się mój pomysł, czy chcesz wymyślić coś zupełnie innego?”";
    }

    private static String bound(String value,int max){
        String text=value.replaceAll("[\\p{Cntrl}]+"," ").trim();
        if (text.length()<=max) return text;
        int end=max;
        if (end>0 && Character.isHighSurrogate(text.charAt(end-1))) end--;
        return text.substring(0,end);
    }
}
