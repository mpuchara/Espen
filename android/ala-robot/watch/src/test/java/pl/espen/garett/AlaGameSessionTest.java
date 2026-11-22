package pl.espen.garett;

import static org.junit.Assert.*;
import java.util.Random;
import org.junit.Test;

public class AlaGameSessionTest {
    static class Stub implements AlaGameSession.Ui {
        String title="", context="", clip="";
        String[] options;
        int cloud, microphone, pulse=-1, reactions;
        boolean available=true;
        public void options(String title,String[] options){this.title=title;this.options=options;}
        public void sayLocally(String message,String[] options){}
        public boolean playButton(String clip,String context){
            cloud++;this.clip=clip;this.context=context;
            return available;
        }
        public void listenForIdea(){microphone++;}
        public void sayIdeaPrompt(String message){}
        public void stopSpeech(){}
        public void status(String message){}
        public void reaction(AlaBehavior.Mood mood){reactions++;}
        public void highlight(int index){pulse=index;}
    }

    @Test public void quizUsesFirmwareBankAndFiveScoredRounds(){
        Stub ui=new Stub();
        AlaGameSession game=new AlaGameSession(ui,new Random(){
            @Override public int nextInt(int bound){return 0;}
        });
        game.open();
        assertEquals(3,ui.options.length);
        game.choose(0);
        assertEquals("15",ui.options[0]);
        for (int choice:new int[]{0,2,1,0,1}) game.choose(choice);
        assertEquals("Wynik: 40/50",ui.title);
        assertEquals(5,ui.reactions);
        game.choose(2);
        assertFalse(game.isRunning());
    }

    @Test public void spokenTopicsQueueEarlyChoiceAndUsePlayedSentence(){
        Stub ui=new Stub();
        AlaGameSession game=new AlaGameSession(ui);
        game.open();
        game.choose(2);
        assertEquals("intro",ui.clip);
        game.onPlayedSentence("Może przeżyjemy przygodę nad jeziorem?");
        assertEquals(1,ui.pulse);
        game.choose(1);
        assertEquals("intro",ui.clip);
        game.onTurnFinished();
        assertEquals(-1,ui.pulse);
        assertEquals("lake",ui.clip);
        assertTrue(game.isNarrating());
        assertNull(ui.options);
    }

    @Test public void narrationContinuesWithAcceptanceAndChildsOwnIdea(){
        Stub ui=new Stub();
        AlaGameSession game=ready(ui);
        game.choose(0);
        assertEquals("story_context",ui.clip);
        assertTrue(ui.context.contains("Nad jeziorem"));
        assertTrue(ui.context.contains("mówiącego łabędzia"));
        assertTrue(ui.context.contains("ostatnią propozycję"));
        game.onPlayedSentence("Na wyspie dzieci znalazły mapę.");
        game.onTurnFinished();
        game.choose(1);
        assertEquals(0,ui.microphone);
        game.choose(0);
        assertEquals(1,ui.microphone);
        game.onVoiceIdea("Chcę latającego smoka.");
        assertTrue(ui.context.contains("mówiącego łabędzia"));
        assertTrue(ui.context.contains("dzieci znalazły mapę"));
        assertTrue(ui.context.contains("latającego smoka"));
        game.onPlayedSentence("Smok ląduje na wyspie.");
        game.onTurnFinished();
        game.choose(2);
        assertTrue(ui.context.contains("zakończenie"));
        game.onPlayedSentence("Wszyscy bezpiecznie wrócili.");
        game.onTurnFinished();
        assertFalse(game.isRunning());
    }

    @Test public void failedContinuationRetriesSameContextAndChildDecision(){
        Stub ui=new Stub();
        AlaGameSession game=ready(ui);
        game.choose(1);
        game.onVoiceIdea("Wyspa odlatuje na księżyc.");
        String original=ui.context;
        game.onTurnFinished(); // No played sentence in this turn.
        assertEquals("Nie udało się opowiedzieć",ui.title);
        game.choose(0);
        assertEquals(original,ui.context);
        game.close();
        game.onTurnFinished();
        assertFalse(game.isRunning());
    }

    @Test public void emptyOrClarificationIdeaDoesNotGenerateChapter(){
        Stub ui=new Stub();
        AlaGameSession game=ready(ui);
        game.choose(1);
        game.onVoiceIdea("");
        game.onVoiceIdea("Mam inny pomysł.");
        assertEquals("Twój pomysł",ui.title);
        assertEquals(2,ui.cloud);
    }

    @Test public void unavailableIntroCanBeRetried(){
        Stub ui=new Stub();
        AlaGameSession game=new AlaGameSession(ui);
        ui.available=false;
        game.open();
        game.choose(2);
        assertEquals("Nie udało się opowiedzieć",ui.title);
        ui.available=true;
        game.choose(0);
        assertEquals("intro",ui.clip);
        assertEquals("Temat opowieści",ui.title);
    }

    private AlaGameSession ready(Stub ui){
        AlaGameSession game=new AlaGameSession(ui);
        game.open();
        game.choose(2);
        game.onTurnFinished();
        game.choose(1);
        game.onPlayedSentence("Ala spotkała mówiącego łabędzia.");
        game.onTurnFinished();
        assertEquals("Co dalej?",ui.title);
        return game;
    }
}
