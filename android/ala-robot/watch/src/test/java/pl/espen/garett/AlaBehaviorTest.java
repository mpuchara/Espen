package pl.espen.garett;

import static org.junit.Assert.*;
import java.util.Random;
import org.junit.Test;

public class AlaBehaviorTest {
    @Test public void idleSettlesByDaypartAndTouchWakesIt(){
        AlaBehavior behavior=new AlaBehavior(275_000,new Random(1));
        assertEquals(AlaBehavior.Mood.NEUTRAL,behavior.mood(275_000,11,false));
        assertEquals(AlaBehavior.Cue.NONE,behavior.idleCue(279_000,11,false));
        assertNotEquals(AlaBehavior.Cue.NONE,behavior.idleCue(282_000,11,false));
        assertEquals(AlaBehavior.Mood.SLEEPY,behavior.mood(365_000,22,false));
        assertEquals(AlaBehavior.Mood.NEUTRAL,behavior.mood(365_000,11,false));
        behavior.react(AlaBehavior.Mood.WINK,365_000);
        assertEquals(AlaBehavior.Mood.WINK,behavior.mood(365_000,22,false));
        assertEquals(AlaBehavior.Mood.NEUTRAL,behavior.mood(374_000,22,false));
    }

    @Test public void phasesSuppressIdleAndCloudEmotionsHaveAfterglow(){
        AlaBehavior behavior=new AlaBehavior(0,new Random(2));
        behavior.phase(AlaBehavior.Phase.THINKING,5000);
        assertEquals(AlaBehavior.Mood.THINKING,behavior.mood(100_000,22,false));
        assertEquals(AlaBehavior.Cue.NONE,behavior.idleCue(100_000,22,false));
        behavior.phase(AlaBehavior.Phase.SPEAKING,100_001);
        behavior.cloud("embarrassed",100_100);
        assertEquals(AlaBehavior.Mood.SHY,behavior.mood(101_000,22,false));
        behavior.phase(AlaBehavior.Phase.IDLE,102_000);
        assertEquals(AlaBehavior.Mood.SHY,behavior.mood(115_000,22,false));
        assertEquals(AlaBehavior.Mood.NEUTRAL,behavior.mood(116_000,22,false));
        assertEquals(AlaBehavior.Cue.NONE,behavior.idleCue(120_000,22,true));
    }

    @Test public void spokenSocialReactionsIgnoreQuotedComplimentsAndFollowTheirAfterglow(){
        AlaBehavior behavior=new AlaBehavior(0,new Random(1));
        behavior.speech("Co znaczy: kocham cię?",100);
        assertEquals(AlaBehavior.Mood.NEUTRAL,behavior.mood(100,12,false));
        behavior.speech("Świetna robota, Ala!",1000);
        assertEquals(AlaBehavior.Mood.SURPRISE,behavior.mood(1200,12,false));
        assertEquals(AlaBehavior.Mood.HAPPY,behavior.mood(4000,12,false));
        behavior.speech("Jest mi smutno.",5000);
        assertEquals(AlaBehavior.Mood.LOVE,behavior.mood(5100,12,false));
    }

    @Test public void strokeAndTickleAreDistinct(){
        AlaSocial.PetMotion motion=new AlaSocial.PetMotion();
        motion.start(100,120,1000);
        assertEquals(AlaBehavior.Mood.LOVE,motion.move(135,120,1100));
        motion.move(105,120,1200);
        motion.move(135,120,1300);
        assertEquals(AlaBehavior.Mood.LAUGH,motion.move(105,120,1400));
    }

    @Test public void allFirmwareAliasesHavePoses(){
        String[] aliases={"neutral","happy","laughing","crying","shocked","annoyed",
            "curious","tired","kissy","relaxed","embarrassed","confused","afraid","silly","bored","delicious"};
        assertEquals(16,AlaBehavior.Mood.values().length);
        for (int i=0;i<aliases.length;i++) {
            AlaBehavior.Mood mood=AlaBehavior.Mood.parse(aliases[i]);
            assertEquals(AlaBehavior.Mood.values()[i],mood);
            assertTrue(mood.eyeWidth>=60);
        }
    }
}
