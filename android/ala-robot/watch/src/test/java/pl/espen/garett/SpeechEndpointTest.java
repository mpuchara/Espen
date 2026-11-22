package pl.espen.garett;

import static org.junit.Assert.*;
import org.junit.Test;

public class SpeechEndpointTest {
    @Test public void shortPhraseEndsAfterFirmwarePause(){
        SpeechEndpoint endpoint=new SpeechEndpoint(0);
        for (int at=60;at<=360;at+=60)
            assertEquals(SpeechEndpoint.Result.CONTINUE,endpoint.frame(1800,at));
        assertEquals(SpeechEndpoint.Result.CONTINUE,endpoint.frame(0,1509));
        assertEquals(SpeechEndpoint.Result.ENDPOINT,endpoint.frame(0,1510));
    }

    @Test public void emptyCaptureAndContinuousVoiceHaveSeparateDeadlines(){
        SpeechEndpoint silent=new SpeechEndpoint(0);
        assertEquals(SpeechEndpoint.Result.CONTINUE,silent.frame(50,4999));
        assertEquals(SpeechEndpoint.Result.NO_SPEECH,silent.frame(50,5000));
        SpeechEndpoint continuous=new SpeechEndpoint(0);
        for (int at=60;at<30_000;at+=60)
            assertEquals(SpeechEndpoint.Result.CONTINUE,continuous.frame(1500,at));
        assertEquals(SpeechEndpoint.Result.MAX_DURATION,continuous.frame(1500,30_000));
    }

    @Test public void sustainedSpeechAndNoisyRoomsAllowLongerPauses(){
        assertEquals(1150,SpeechEndpoint.pauseMs(1799,100));
        assertEquals(1600,SpeechEndpoint.pauseMs(1800,100));
        assertEquals(2150,SpeechEndpoint.pauseMs(3500,850));
        assertEquals(2400,SpeechEndpoint.pauseMs(3500,851));
    }
}
