package pl.espen.garett;

/** P4 adaptive endpoint timing driven by Android's 16 kHz microphone RMS. */
final class SpeechEndpoint {
    enum Result { CONTINUE, ENDPOINT, NO_SPEECH, MAX_DURATION }

    private final long started;
    private long lastVoice;
    private int voicedMs, consecutive;
    private double noise=95;

    SpeechEndpoint(long now){
        started=lastVoice=now;
    }

    Result frame(int rms,long now){
        boolean voice=rms>Math.max(230,noise*2);
        if (voice) {
            if (++consecutive>=2) {
                voicedMs+=voicedMs==0 ? 100 : (int)Math.min(100,Math.max(0,now-lastVoice));
                lastVoice=now;
            }
        } else {
            consecutive=0;
            if (voicedMs==0) noise=noise*.96+rms*.04;
        }
        if (now-started>=30_000) return Result.MAX_DURATION;
        if (voicedMs<240 && now-lastVoice>=5_000) return Result.NO_SPEECH;
        if (voicedMs>=240 && now-lastVoice>=pauseMs(voicedMs,noise)) return Result.ENDPOINT;
        return Result.CONTINUE;
    }

    static int pauseMs(int voicedMs,double noise){
        int pause=voicedMs<1800 ? 1150 : voicedMs<3500 ? 1600 : 2150;
        return pause+(noise>850 ? 250 : 0);
    }
}
