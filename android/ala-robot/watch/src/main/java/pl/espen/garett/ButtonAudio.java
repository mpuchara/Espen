package pl.espen.garett;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Reads the pinned 16 kHz / 60 ms Opus button prompts shared with P4. */
final class ButtonAudio {
    private ButtonAudio() {}

    static List<byte[]> read(InputStream input) throws IOException {
        List<byte[]> frames=new ArrayList<>();
        ByteArrayOutputStream packet=new ByteArrayOutputStream();
        byte[] header=new byte[27];
        byte[] segments=new byte[255];
        byte[] data=new byte[255];
        int index=0;
        while (input.read(header,0,1) != -1) {
            readFully(input,header,1,26);
            if (header[0]!='O' || header[1]!='g' || header[2]!='g' || header[3]!='S' || header[4]!=0)
                throw new IOException("Nieprawidłowy nagłówek Ogg");
            int count=header[26]&255;
            readFully(input,segments,0,count);
            for (int i=0;i<count;i++) {
                int length=segments[i]&255;
                readFully(input,data,0,length);
                packet.write(data,0,length);
                if (packet.size()>8192) throw new IOException("Zbyt duży pakiet Ogg");
                if (length==255) continue;
                byte[] complete=packet.toByteArray();
                packet.reset();
                if (index==0 && (!tag(complete,"OpusHead") || complete.length<19 || complete[9]!=1))
                    throw new IOException("Nieprawidłowy nagłówek Opus");
                if (index==1 && !tag(complete,"OpusTags")) throw new IOException("Brak metadanych Opus");
                if (index++<2) continue;
                if (complete.length==0 || complete.length>960 || frames.size()==200)
                    throw new IOException("Pakiet dźwiękowy poza zakresem");
                frames.add(complete);
            }
        }
        if (frames.isEmpty() || packet.size()!=0) throw new IOException("Niekompletny komunikat audio");
        return frames;
    }

    private static boolean tag(byte[] packet,String prefix) {
        return packet.length>=prefix.length() &&
            new String(packet,0,prefix.length(),StandardCharsets.US_ASCII).equals(prefix);
    }

    private static void readFully(InputStream input,byte[] bytes,int offset,int count) throws IOException {
        while (count>0) {
            int read=input.read(bytes,offset,count);
            if (read<0) throw new IOException("Ucięty strumień Ogg");
            count-=read;
            offset+=read;
        }
    }
}
