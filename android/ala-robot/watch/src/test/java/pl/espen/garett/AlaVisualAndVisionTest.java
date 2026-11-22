package pl.espen.garett;

import static org.junit.Assert.*;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Test;

public final class AlaVisualAndVisionTest {
    @Test public void firmwareScenesAndEmotionBeatsCoverStoryContexts() throws Exception {
        assertEquals(NarrationCue.Scene.Dragon,NarrationCue.scene("Przyleciał smok Iskra."));
        assertEquals(NarrationCue.Scene.Forest,NarrationCue.scene("W głębi lasu coś świeciło."));
        assertEquals(AlaBehavior.Mood.SURPRISE,NarrationCue.mood("Ale niespodzianka!"));
        assertEquals(AlaBehavior.Mood.SAD,NarrationCue.mood("Bohater zasmucony patrzył na mapę."));
        assertEquals(AlaBehavior.Mood.LOVE,NarrationCue.mood("„Jestem przy tobie” powiedziała Ala."));
        byte[] data=Files.readAllBytes(Path.of("src/main/assets/ala/visual-world.bin"));
        VisualFrames frames=new VisualFrames(data);
        assertEquals(0xff64dca0,frames.tint(NarrationCue.Scene.Dragon));
        assertThrows(IOException.class,()->new VisualFrames(java.util.Arrays.copyOf(data,data.length-1)));
    }

    @Test public void onlyTheAuditedPublicLegacyEndpointIsUpgraded(){
        assertEquals("https://api.xiaozhi.me/vision/explain",
            Vision.secureEndpoint("http://api.xiaozhi.me/vision/explain").toString());
        assertNotNull(Vision.secureEndpoint("https://localhost:8443/vision"));
        assertNull(Vision.secureEndpoint(null));
        assertNull(Vision.secureEndpoint("http://api.xiaozhi.me/vision/explain?token=x"));
        assertNull(Vision.secureEndpoint("http://api.xiaozhi.me.example.org/vision/explain"));
        assertNull(Vision.secureEndpoint("https://user@api.xiaozhi.me/vision/explain"));
        assertNull(Vision.secureEndpoint("https://api.xiaozhi.me/vision/explain\n"));
    }

    @Test public void invalidJpegCannotBeUploaded(){
        assertFalse(Vision.jpegHeader(null));
        assertFalse(Vision.jpegHeader(new byte[]{1,2,3,4}));
        assertTrue(Vision.jpegHeader(new byte[]{(byte)255,(byte)216,(byte)255,(byte)224}));
    }
}
