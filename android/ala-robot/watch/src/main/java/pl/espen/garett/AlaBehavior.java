package pl.espen.garett;

import java.util.Locale;
import java.util.Random;

/** Visual policy shared by both APKs. Timings and poses follow the P4 0.2.28 policy. */
final class AlaBehavior {
    enum Phase { IDLE, CONNECTING, LISTENING, THINKING, CAPTURING, VISION_ANALYZING, SPEAKING, ERROR }
    enum Accent { NONE, HEART, STAR, TEAR, BLUSH, DOTS, SLEEP }
    enum Cue { LEFT, RIGHT, UP, DOWN, BLINK, SMILE, TILT, SQUINT, DOUBLE_BLINK, NONE }

    enum Mood {
        NEUTRAL(44,76,0,0,0,88,8,14,0x63E6FF,Accent.NONE),
        HAPPY(28,79,0,-8,0,116,9,24,0x7CF6DA,Accent.NONE,"happy"),
        LAUGH(9,82,0,-12,0,109,50,0,0x7CF6DA,Accent.NONE,"laugh","laughing","funny"),
        SAD(30,72,-13,15,0,74,8,-22,0x83ACFF,Accent.TEAR,"sad","crying"),
        SURPRISE(70,66,0,-22,0,39,42,0,0x63E6FF,Accent.NONE,"surprise","surprised","shocked"),
        ANGRY(25,78,15,21,0,76,7,-12,0xFF907B,Accent.NONE,"angry","annoyed"),
        THINKING(39,74,0,6,9,59,8,0,0x63E6FF,Accent.DOTS,"thinking","curious"),
        SLEEPY(8,79,0,3,0,64,7,0,0x74A7BD,Accent.SLEEP,"sleepy","sleeping","tired"),
        LOVE(51,69,0,-8,0,112,9,23,0xFF91C6,Accent.HEART,"loving","kissy","love"),
        CONFIDENT(23,83,0,4,5,96,7,13,0x66D9FF,Accent.NONE,"confident","cool","relaxed"),
        SHY(31,72,-6,8,0,75,7,13,0xFFADC9,Accent.BLUSH,"shy","embarrassed"),
        CONFUSED(44,70,0,9,25,56,8,-6,0xA9BFFF,Accent.NONE,"confused"),
        FEAR(66,60,-10,-13,0,43,28,0,0xA9BFFF,Accent.NONE,"afraid","fear","scared"),
        WINK(38,76,0,-6,35,100,8,19,0x7CF6DA,Accent.NONE,"wink","winking","silly"),
        BORED(17,81,0,8,0,65,7,0,0x74A7BD,Accent.NONE,"bored"),
        EXCITED(64,74,0,-18,0,120,12,25,0xFFE195,Accent.STAR,"excited","delicious");

        final int eyeHeight, eyeWidth, tilt, brow, asymmetry, mouthWidth, mouthHeight, smile, color;
        final Accent accent;
        private final String[] aliases;
        Mood(int eyeHeight, int eyeWidth, int tilt, int brow, int asymmetry, int mouthWidth,
             int mouthHeight, int smile, int color, Accent accent, String... aliases) {
            this.eyeHeight=eyeHeight; this.eyeWidth=eyeWidth; this.tilt=tilt; this.brow=brow;
            this.asymmetry=asymmetry; this.mouthWidth=mouthWidth; this.mouthHeight=mouthHeight;
            this.smile=smile; this.color=color | 0xff000000; this.accent=accent; this.aliases=aliases;
        }
        static Mood parse(String value) {
            if (value == null) return NEUTRAL;
            String alias=value.toLowerCase(Locale.ROOT);
            for (Mood mood : values()) for (String name : mood.aliases) {
                if (name.equals(alias)) return mood;
            }
            return NEUTRAL;
        }
    }

    private static final Cue[] CUES=Cue.values();
    private final Random random;
    private Phase phase=Phase.IDLE;
    private Mood cloud=Mood.NEUTRAL, direct=Mood.NEUTRAL, afterglow=Mood.NEUTRAL;
    private AlaSocial.Reaction social=AlaSocial.Reaction.NONE;
    private long socialAt;
    private long interactionAt, cueAt, nextCue, cloudUntil, directUntil, afterglowUntil;
    private Cue cue=Cue.NONE, previousCue=Cue.NONE;

    AlaBehavior(long now, Random random) {
        this.random=random;
        interact(now);
    }

    void interact(long now) {
        interactionAt=now;
        nextCue=now+6_500;
        cue=Cue.NONE;
    }

    void phase(Phase next, long now) {
        if (phase == next) return;
        if (phase == Phase.SPEAKING && cloud != Mood.NEUTRAL && now < cloudUntil) {
            afterglow=cloud;
            afterglowUntil=now+14_000;
        }
        phase=next;
        interact(now);
    }

    void cloud(String emotion, long now) {
        Mood incoming=Mood.parse(emotion);
        if (incoming == Mood.NEUTRAL && now < cloudUntil) return;
        cloud=incoming;
        cloudUntil=now+7_500;
    }

    void react(Mood mood, long now) {
        interact(now);
        direct=mood;
        directUntil=now+1_700;
        afterglow=mood;
        afterglowUntil=now+7_000;
    }

    void speech(String text,long now){
        AlaSocial.Reaction reaction=AlaSocial.classify(text);
        if (reaction==AlaSocial.Reaction.NONE) return;
        interact(now);
        social=reaction;
        socialAt=now;
        afterglow=reaction.afterglow;
        afterglowUntil=now+14_000;
    }

    private Mood reply=Mood.NEUTRAL;
    private long replyUntil;

    void reply(Mood mood,long now){
        reply=mood;
        replyUntil=now+1800;
    }

    Mood mood(long now, int hour, boolean game) {
        if (phase == Phase.ERROR) return Mood.SAD;
        if (now < directUntil) return direct;
        if (social!=AlaSocial.Reaction.NONE && now-socialAt<2400)
            return now-socialAt<650 ? social.initial : social.afterglow;
        if (phase == Phase.CONNECTING || phase == Phase.THINKING || phase == Phase.VISION_ANALYZING) return Mood.THINKING;
        if (now < cloudUntil && cloud != Mood.NEUTRAL && cloud != Mood.HAPPY) return cloud;
        if (phase==Phase.SPEAKING && now<replyUntil && reply!=Mood.NEUTRAL) return reply;
        if (now < cloudUntil && cloud != Mood.NEUTRAL) return cloud;
        if (phase != Phase.IDLE || game) return Mood.NEUTRAL;
        if (now < afterglowUntil) return afterglow;
        return asleep(now,hour) ? Mood.SLEEPY : Mood.NEUTRAL;
    }

    private boolean asleep(long now, int hour) {
        return now-interactionAt >= (night(hour) ? 90_000 : hour >= 19 ? 150_000 : 210_000);
    }

    private static boolean night(int hour) { return hour >= 22 || hour < 6; }

    Cue idleCue(long now, int hour, boolean game) {
        if (phase != Phase.IDLE || game || asleep(now,hour)) return Cue.NONE;
        if (now >= nextCue) {
            boolean calm=now-interactionAt >= 45_000;
            cue=CUES[random.nextInt(calm ? 8 : 9)];
            if (cue == previousCue) cue=cue == Cue.LEFT ? Cue.RIGHT : Cue.LEFT;
            previousCue=cue;
            cueAt=now;
            nextCue=now+9_230+random.nextInt(9_700)+(calm ? 4_500 : 0)
                +(night(hour) ? 9_500 : hour >= 19 ? 3_500 : 0);
        }
        return now-cueAt < (cue == Cue.SMILE || cue == Cue.TILT ? 1_650 : 1_250)
            ? cue : Cue.NONE;
    }

    boolean cueBlink(long now, Cue active) {
        long elapsed=now-cueAt;
        return (active == Cue.BLINK || active == Cue.DOUBLE_BLINK) &&
            (elapsed < 180 || active == Cue.DOUBLE_BLINK && elapsed >= 260 && elapsed < 440);
    }
}
