package pl.espen.garett;

import static org.junit.Assert.*;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import org.junit.Test;
import okio.ByteString;
import io.github.jaredmdobson.concentus.OpusDecoder;

public class XiaozhiWireTest {
    @Test public void everyFirmwarePromptIsMonoOpusWithSixtyMillisecondFrames() throws Exception {
        for (String clip:new String[]{"intro","forest","lake","car","story_context","camera"}) {
            Path file=Path.of("src/main/assets/ala",clip+".ogg");
            byte[] bytes=Files.readAllBytes(file);
            OpusDecoder decoder=new OpusDecoder(16000,1);
            short[] pcm=new short[1920];
            try (ByteArrayInputStream input=new ByteArrayInputStream(bytes)) {
                for (byte[] packet:ButtonAudio.read(input))
                    assertEquals(clip,960,decoder.decode(packet,0,packet.length,pcm,0,pcm.length,false));
            }
            byte[] truncated=Arrays.copyOf(bytes,bytes.length-1);
            assertThrows(IOException.class,()->ButtonAudio.read(new ByteArrayInputStream(truncated)));
        }
    }

    @Test public void versionsPreservePacketAndNetworkByteOrder() throws Exception {
        byte[] data={0x61,0x62,0x63};
        for (int version=1;version<=3;version++)
            assertArrayEquals(data,WsAudio.decode(WsAudio.encode(data,version,0x50607080L),version));
        assertEquals("00020000000000005060708000000003616263",WsAudio.encode(data,2,0x50607080L).hex());
        assertEquals("00000003616263",WsAudio.encode(data,3,0).hex());
    }

    @Test public void malformedAndNonAudioFramesCannotReachDecoder(){
        assertThrows(IOException.class,()->WsAudio.decode(ByteString.EMPTY,1));
        assertThrows(IOException.class,()->WsAudio.decode(ByteString.decodeHex("0100000161"),3));
        assertThrows(IOException.class,()->WsAudio.decode(ByteString.decodeHex("0000000261"),3));
        assertThrows(IOException.class,()->WsAudio.decode(ByteString.decodeHex("0001000000000000000000000000000161"),2));
    }

    @Test public void visionRequiresAnExplicitRequestAndHonorsNegativeCommands(){
        assertTrue(Vision.isVisionQuestion("Alu, zrób zdjęcie."));
        assertTrue(Vision.isVisionQuestion("Ala co widzisz?"));
        assertFalse(Vision.isVisionQuestion("Nie rób zdjęcia"));
        assertFalse(Vision.isVisionQuestion("Co to za historia?"));
        assertFalse(Vision.isVisionQuestion(null));
    }
}
