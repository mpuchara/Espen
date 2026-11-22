package pl.espen.garett;

import java.io.IOException;
import java.nio.ByteBuffer;
import okio.ByteString;

final class WsAudio {
    private WsAudio() {}

    static ByteString encode(byte[] opus,int version,long timestamp) {
        if (version==1) return ByteString.of(opus);
        ByteBuffer out=ByteBuffer.allocate((version==2 ? 16 : 4)+opus.length);
        if (version==2) {
            out.putShort((short)2).putShort((short)0).putInt(0)
                .putInt((int)timestamp).putInt(opus.length);
        } else {
            out.putShort((short)0).putShort((short)opus.length);
        }
        return ByteString.of(out.put(opus).array());
    }

    static byte[] decode(ByteString frames,int version) throws IOException {
        int header=version==1 ? 0 : version==2 ? 16 : 4;
        if (frames.size()<=header) throw new IOException("Ucięta ramka Opus");
        ByteBuffer in=frames.asByteBuffer();
        int size=frames.size();
        if (version==2) {
            if (in.getShort()!=2 || in.getShort()!=0) throw new IOException("Typ ramki v2");
            in.position(12);
            size=in.getInt();
        } else if (version==3) {
            if (in.getShort()!=0) throw new IOException("Typ ramki v3");
            size=in.getShort()&0xffff;
        }
        if (size!=in.remaining()) throw new IOException("Nieprawidłowa długość Opus");
        byte[] data=new byte[size];
        in.get(data);
        return data;
    }
}
