package pl.espen.garett;
import static org.junit.Assert.*;
import org.junit.Test;
public class AlaGameSessionTest {
    static class Stub implements AlaGameSession.Ui{
        String title="",lastCloud="";String[] options;int local,cloud;
        public void options(String t,String[] o){title=t;options=o;}
        public void sayLocally(String m,String[] o){local++;}
        public boolean sayWithAla(String p){cloud++;lastCloud=p;return true;}
        public void listenForIdea(){}
        public void stopSpeech(){}
        public void status(String s){}
    }
    @Test public void quizThenClose(){
        Stub stub=new Stub();AlaGameSession g=new AlaGameSession(stub);
        g.open();assertEquals(3,stub.options.length);
        g.choose(0);assertEquals("15",stub.options[0]);
        for(int i=0;i<5;i++)g.choose(0);
        assertTrue(stub.title.startsWith("Wynik:"));assertEquals(3,stub.options.length);
        g.choose(2);assertFalse(g.isRunning());
    }
    @Test public void topicAndVoiceIdeaPreserveContext(){
        Stub stub=new Stub();AlaGameSession g=new AlaGameSession(stub);
        g.open();g.choose(2);assertEquals("Temat opowieści",stub.title);
        g.choose(1);assertTrue(g.isNarrating());assertTrue(stub.lastCloud.contains("Nad jeziorem"));
        g.onStorySentence("Ala spotkała mówiącego łabędzia.");g.onTurnFinished();
        assertEquals("Co dalej?",stub.title);
        g.choose(1);g.onVoiceIdea("Chcę latającego smoka.");
        assertTrue(stub.lastCloud.contains("mówiącego łabędzia"));
        assertTrue(stub.lastCloud.contains("latającego smoka"));
        assertEquals(2,stub.cloud);
    }
    @Test public void emptyIdeaDoesNotStartNewStory(){
        Stub s=new Stub();AlaGameSession g=new AlaGameSession(s);
        g.open();g.choose(2);g.choose(0);g.onTurnFinished();g.choose(1);g.onVoiceIdea("");
        assertEquals(1,s.cloud);
    }
}
