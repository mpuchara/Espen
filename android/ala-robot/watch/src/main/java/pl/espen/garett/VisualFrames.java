package pl.espen.garett;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** Read-only sampled P4 primitives. Frame lookup and drawing allocate no objects. */
final class VisualFrames {
    private final byte[] data;
    private final int[][] offsets;
    private final int[] colors;
    private final int frames;
    private static volatile VisualFrames cached;

    static VisualFrames load(Context context){
        VisualFrames current=cached;
        if (current!=null) return current;
        try (InputStream source=context.getAssets().open("ala/visual-world.bin")) {
            ByteArrayOutputStream output=new ByteArrayOutputStream();
            byte[] chunk=new byte[8192];
            for (int count;(count=source.read(chunk))!=-1;) {
                if (output.size()+count>2_000_000) throw new IOException("Visual World data");
                output.write(chunk,0,count);
            }
            cached=new VisualFrames(output.toByteArray());
            return cached;
        } catch (IOException error) {
            android.util.Log.w("AlaFace","Visual World asset unavailable",error);
            return null;
        }
    }

    VisualFrames(byte[] data) throws IOException {
        this.data=data;
        int scenes=NarrationCue.Scene.values().length;
        if (data.length<6 || data[0]!='A' || data[1]!='L' || data[2]!='A' || data[3]!='V'
            || unsigned(4)!=scenes || unsigned(5)!=48) throw new IOException("Invalid P4 frame header");
        frames=unsigned(5);
        offsets=new int[scenes][frames*2];
        colors=new int[scenes];
        int cursor=6;
        for (int scene=0;scene<scenes;scene++) {
            if (cursor+3>data.length) throw new IOException("Truncated P4 tint");
            colors[scene]=color(cursor);
            cursor+=3;
            for (int frame=0;frame<frames*2;frame++) {
                if (cursor>=data.length || unsigned(cursor)>72) throw new IOException("Invalid P4 frame");
                offsets[scene][frame]=cursor;
                cursor+=1+unsigned(cursor)*9;
            }
        }
        if (cursor!=data.length) throw new IOException("Invalid P4 data length");
    }

    private int unsigned(int offset){return data[offset]&255;}
    private int color(int offset){
        return 0xff000000 | unsigned(offset)<<16 | unsigned(offset+1)<<8 | unsigned(offset+2);
    }
    int tint(NarrationCue.Scene scene){return colors[scene.ordinal()];}

    void draw(Canvas canvas,Paint paint,NarrationCue.Scene scene,long elapsed,boolean watch){
        int frame=(int)(elapsed/100%frames)*2;
        drawAt(canvas,paint,offsets[scene.ordinal()][frame],watch?296:294,166,watch?44:48);
        canvas.save();
        canvas.clipRect(0,36,360,107);
        drawAt(canvas,paint,offsets[scene.ordinal()][frame+1],120,39,120);
        canvas.restore();
    }

    private void drawAt(Canvas canvas,Paint paint,int offset,float x,float y,float size){
        int count=unsigned(offset++);
        if (count==0) return;
        canvas.save();
        canvas.translate(x,y);
        canvas.scale(size/126f,size/126f);
        paint.setShader(null);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStyle(Paint.Style.FILL);
        for (int i=0;i<count;i++,offset+=9) {
            int px=unsigned(offset),py=unsigned(offset+1),w=unsigned(offset+2),h=unsigned(offset+3);
            int radius=unsigned(offset+4);
            paint.setColor(color(offset+6));
            if (unsigned(offset+5)==1) {
                paint.setStrokeWidth(radius);
                canvas.drawLine(px,py,w,h,paint);
            } else canvas.drawRoundRect(px,py,px+w,py+h,radius,radius,paint);
        }
        canvas.restore();
    }
}
