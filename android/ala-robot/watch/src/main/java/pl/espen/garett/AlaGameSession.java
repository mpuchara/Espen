package pl.espen.garett;

/**
 * Platform-neutral, foreground-only Ala game state. The same contract powers phone and Essa.
 * All callbacks are invoked by the Android UI thread; no networking is done here.
 */
public final class AlaGameSession {
    public interface Ui {
        void options(String title, String[] choices);
        void sayLocally(String message, String[] choices);
        boolean sayWithAla(String prompt);
        void listenForIdea();
        void stopSpeech();
        void status(String message);
    }
    private enum Phase { OFF, MENU, THEMES, QUIZ, ANIMALS, END, STORY_SPEAKING, STORY_CHOICE, STORY_IDEA }
    private static final String[] MENU={"Quiz Mistrza","Zgadnij zwierzę","Magiczna opowieść"};
    private static final String[] THEMES={"Magiczny las","Nad jeziorem","Z mamą w samochodzie"};
    private static final String[] STORY_DECISIONS={"Podoba mi się","Mam inny pomysł","Koniec opowieści"};
    private static final String[][] QUIZ={
        {"Ile to siedem plus osiem?","15","14","16","0"},
        {"Która planeta jest najbliżej Słońca?","Mars","Merkury","Neptun","1"},
        {"Ile nóg ma pająk?","6","8","10","1"},
        {"Co wyrasta z nasionka?","Roślina","Kamień","Chmura","0"},
        {"Ile minut ma godzina?","100","30","60","2"},
    };
    private static final String[][] ANIMALS={
        {"Mam długą szyję i jem liście z drzew. Kim jestem?","Żyrafa","Lis","Pingwin","0"},
        {"Jestem ogromny, szary i mam trąbę. Kim jestem?","Słoń","Hipopotam","Nosorożec","0"},
        {"Mam czarno-białe pasy i mieszkam w Afryce.","Koń","Zebra","Tygrys","1"},
        {"Nie latam, ale pływam w zimnym morzu.","Wróbel","Pingwin","Sokół","1"},
        {"Mam osiem ramion i mieszkam w morzu.","Krab","Ośmiornica","Meduza","1"},
    };
    private final Ui ui;
    private Phase phase=Phase.OFF;
    private String theme="", history="", lastSpoken="";
    private int round, score, storyTurn;
    private Phase previousQuiz=Phase.QUIZ;
    public AlaGameSession(Ui ui){this.ui=ui;}
    public boolean isRunning(){return phase!=Phase.OFF;}
    public boolean isNarrating(){return phase==Phase.STORY_SPEAKING;}
    public void open(){
        if(phase!=Phase.OFF)ui.stopSpeech();
        phase=Phase.MENU;
        ui.options("Gry Ali",MENU);
        ui.sayLocally("Zagrajmy! Wybierz jedną z gier.",MENU);
    }
    public void close(){
        phase=Phase.OFF;history="";lastSpoken="";theme="";
        ui.stopSpeech();ui.options("",null);
    }
    public void choose(int index){
        if(index<0||index>2)return;
        switch(phase){
            case MENU:
                if(index==2){
                    phase=Phase.THEMES;
                    ui.options("Temat opowieści",THEMES);
                    ui.sayLocally("Wybierz temat naszej magicznej przygody.",THEMES);
                }else{
                    phase=index==0?Phase.QUIZ:Phase.ANIMALS;
                    previousQuiz=phase;
                    round=0;score=0;askQuestion();
                }
                break;
            case THEMES:
                theme=THEMES[index];storyTurn=0;history="";lastSpoken="";
                continueStory("");
                break;
            case QUIZ: case ANIMALS:
                String[][] questions=phase==Phase.QUIZ?QUIZ:ANIMALS;
                boolean good=index==Integer.parseInt(questions[round][4]);
                if(good)score+=10;
                round++;
                if(round==questions.length){
                    phase=Phase.END;
                    ui.options("Wynik: "+score+"/"+(questions.length*10),
                        new String[]{"Jeszcze raz","Gry","Zamknij"});
                    ui.sayLocally(good?"Brawo! Zakończyliśmy grę.":"To była dobra zabawa! Koniec gry.",
                        new String[]{"Jeszcze raz","Gry","Zamknij"});
                }else{
                    askQuestion(good?"Brawo, poprawna odpowiedź! ":"Spróbujemy dalej. ");
                }
                break;
            case END:
                if(index==0){phase=previousQuiz;round=0;score=0;askQuestion();}
                else if(index==1)open();
                else close();
                break;
            case STORY_CHOICE:
                if(index==0)continueStory("Dziecko akceptuje ostatni pomysł Ali. Zrealizuj go.");
                else if(index==1){
                    phase=Phase.STORY_IDEA;
                    ui.options("Twój pomysł",new String[]{"Powiedz do mikrofonu","Wróć do pomysłów","Zamknij"});
                    ui.sayLocally("Opowiedz mi, co ma się wydarzyć! Dotknij pierwszej opcji i powiedz swój pomysł.",null);
                }else{
                    close();
                    ui.status("Do zobaczenia w następnej przygodzie!");
                }
                break;
            case STORY_IDEA:
                if(index==0)ui.listenForIdea();
                else if(index==1){phase=Phase.STORY_CHOICE;ui.options("Co dalej?",STORY_DECISIONS);}
                else close();
                break;
            default:break;
        }
    }
    private void askQuestion(){askQuestion("");}
    private void askQuestion(String prefix){
        String[] q=(phase==Phase.QUIZ?QUIZ:ANIMALS)[round];
        String[] opts={q[1],q[2],q[3]};
        ui.options((phase==Phase.QUIZ?"Quiz":"Zwierzęta")+" · "+(round+1)+"/5 · "+score+"⭐",opts);
        ui.sayLocally(prefix+q[0],opts);
    }
    public void onStorySentence(String text){
        if(phase!=Phase.STORY_SPEAKING||text==null)return;
        if(lastSpoken.length()<1600)lastSpoken+=bounded(text,260)+" ";
    }
    public void onTurnFinished(){
        if(phase!=Phase.STORY_SPEAKING)return;
        if(!lastSpoken.isEmpty()){
            history=bounded(history+" "+lastSpoken,1300);
        }
        phase=Phase.STORY_CHOICE;
        ui.options("Co dalej?",STORY_DECISIONS);
        // The closing spoken question already explains the options; no overlapping local TTS.
    }
    public void onVoiceIdea(String spoken){
        if(phase!=Phase.STORY_IDEA)return;
        String idea=bounded(spoken,220);
        if(idea.trim().isEmpty()){
            ui.status("Nie rozpoznałam pomysłu. Spróbuj jeszcze raz.");
            return;
        }
        continueStory("Dziecko wybrało własny dalszy bieg wydarzeń: «"+idea+"». " +
            "W kolejnej części przedstaw jego pomysł jako główną przyczynę wydarzeń.");
    }
    private void continueStory(String decision){
        phase=Phase.STORY_SPEAKING;storyTurn++;lastSpoken="";
        ui.options("",null); // full animated eyes and mouth while Xiaozhi speaks
        StringBuilder p=new StringBuilder("Jesteś Alą, ciepłą narratorką zabawnej Magicznej Opowieści dla dziecka. ")
          .append("Mów wyłącznie po polsku, w pierwszej osobie, bez list i technicznych komentarzy. ")
          .append("Miejsce: ").append(theme).append(". ");
        if(theme.equals(THEMES[2]))p.append("Mama jest bezpiecznym kierowcą i zatrzymuje się przed wspólną zabawą. ");
        if(storyTurn==1)p.append("Przedstaw bohaterów, świat i pierwszą niezwykłą przygodę. ");
        else p.append("Kontynuuj tę samą historię, zachowaj postacie i kontekst: «")
            .append(bounded(history,1250)).append("». ").append(decision).append(" ");
        p.append("Opowiedz 5 do 8 krótkich zdań. Wybierz jeden ciekawy następny krok fabuły i zapytaj na głos: ")
         .append("„Podoba ci się mój pomysł, czy wolisz wymyślić coś zupełnie innego?” ")
         .append("Po pytaniu zakończ wypowiedź, aby dziecko mogło zdecydować.");
        if(!ui.sayWithAla(p.toString())){
            phase=Phase.STORY_CHOICE;
            ui.options("Spróbuj ponownie",STORY_DECISIONS);
            ui.status("Nie udało się rozpocząć narracji Xiaozhi.");
        }
    }
    private static String bounded(String value,int max){
        if(value==null)return "";
        String clean=value.replaceAll("[\\p{Cntrl}]+"," ").trim();
        return clean.length()<=max?clean:clean.substring(clean.length()-max);
    }
}
