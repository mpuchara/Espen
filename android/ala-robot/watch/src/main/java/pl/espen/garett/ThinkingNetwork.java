package pl.espen.garett;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;

/** A decorative graph with bounded intensity and a shared timing envelope. */
final class ThinkingNetwork {
    enum Mode { OFF, REPLY, STORY, VISION }
    private static final int[] X={108,143,179,217,252,128,164,197,234,171,192};
    private static final int[] Y={72,49,69,48,70,92,91,90,91,45,108};
    private static final int[] EDGES={0,1,1,2,2,3,3,4,0,5,1,5,2,6,2,7,3,8,4,8,
        5,6,6,7,7,8,1,9,9,2,9,3,6,10,7,10};
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private Mode mode=Mode.OFF, lastMode=Mode.REPLY;
    private long started,changed;
    private float from;

    void mode(Mode next,long now){
        if (mode==next) return;
        from=opacity(now);
        changed=now;
        if (next!=Mode.OFF) {
            if (mode==Mode.OFF) started=now;
            lastMode=next;
        }
        mode=next;
    }

    void clear(){mode=Mode.OFF;from=0;}
    private float opacity(long now){
        float elapsed=Math.max(0,now-changed);
        float progress=Math.min(1,elapsed/(mode==Mode.OFF?420f:460f));
        float eased=progress*progress*(3-2*progress);
        return mode==Mode.OFF?from*(1-eased):from+(1-from)*eased;
    }
    boolean visible(long now){return opacity(now)>.001f;}

    void draw(Canvas canvas,long now,boolean compact){
        float fade=opacity(now);
        if (fade<=0) return;
        long age=now-started;
        int count=compact?12:EDGES.length/2;
        int pulses=compact?2:age>=3500?5:3;
        for (int i=0;i<count;i++) {
            int a=EDGES[i*2],b=EDGES[i*2+1];
            float wave=(float)(.5+.5*Math.sin(age/460.0+i*.62));
            float reveal=Math.min(1,Math.max(0,(age-i*19)/260f));
            paint.setStrokeWidth(1.15f);
            paint.setColor(Color.argb((int)((35+54*wave)*fade*reveal),104,199,236));
            canvas.drawLine(X[a],Y[a],X[b],Y[b],paint);
            if ((i+(int)(age/520))%Math.max(2,count/pulses)==0) {
                if (lastMode==Mode.VISION && Math.abs(X[b]-180)<Math.abs(X[a]-180)) {
                    int swap=a;a=b;b=swap;
                }
                float progress=((age+i*173)%1180)/1180f;
                float x=X[a]+(X[b]-X[a])*progress,y=Y[a]+(Y[b]-Y[a])*progress;
                paint.setColor(Color.argb((int)(173*fade*reveal),206,245,255));
                canvas.drawCircle(x,y,2.5f,paint);
            }
        }
        for (int i=0;i<(compact?9:X.length);i++) {
            float wave=(float)(.5+.5*Math.sin(age/260.0+i*.71));
            float reveal=Math.min(1,Math.max(0,(age-i*24)/200f))*fade;
            paint.setColor(Color.argb((int)(38*reveal),96,217,255));
            canvas.drawCircle(X[i],Y[i],5+wave*1.8f,paint);
            paint.setColor(Color.argb((int)((130+112*wave)*reveal),i%3==0?179:108,
                i%3==0?158:238,255));
            canvas.drawCircle(X[i],Y[i],2.1f+wave,paint);
        }
    }
}
