package pl.espen.alacontroller;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.view.View;

/** Small, allocation-free frame-by-frame renderer; no assets or permissions. */
public final class FaceView extends View {
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private String mood = "neutral";
    private boolean blush, permanent;
    private long untilMs = 0;
    private long scheduledMs = 0;
    private String scheduledMood = "";
    public FaceView(Context context) {
        super(context);
        setLayerType(View.LAYER_TYPE_SOFTWARE, null);
    }
    public synchronized void show(String emotion, boolean showBlush, boolean forever, int seconds) {
        mood=emotion;
        blush=showBlush || "shy".equals(emotion) || "love".equals(emotion);
        permanent=forever;
        scheduledMs=0;scheduledMood="";
        untilMs=SystemClock.uptimeMillis()+seconds*1000L;
        invalidate();
    }
    public synchronized void waiting(String emotion,int seconds){
        scheduledMood=emotion;scheduledMs=SystemClock.uptimeMillis()+seconds*1000L;
        invalidate();
    }
    public synchronized void reset(){
        mood="neutral";blush=false;permanent=false;untilMs=0;scheduledMs=0;scheduledMood="";
        invalidate();
    }
    public synchronized String getMood(){return mood;}
    private void clr(int color){p.reset();p.setAntiAlias(true);p.setColor(color);}
    private void rounded(Canvas c,float a,float b,float x,float y,float r,int color) {
        clr(color);c.drawRoundRect(new RectF(a,b,x,y),r,r,p);
    }
    private void line(Canvas c,float x1,float y1,float x2,float y2,int color,float width) {
        clr(color);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(width);p.setStrokeCap(Paint.Cap.ROUND);
        c.drawLine(x1,y1,x2,y2,p);
    }
    private void text(Canvas c,String s,int x,int y,int size,int color) {
        clr(color);p.setTextSize(size);p.setTextAlign(Paint.Align.CENTER);
        p.setTypeface(android.graphics.Typeface.create("sans-serif-medium",0));
        c.drawText(s,x,y,p);
    }
    @Override protected synchronized void onDraw(Canvas actual){
        super.onDraw(actual);
        int w=getWidth(),h=getHeight();
        if(w==0||h==0)return;
        actual.drawColor(Color.rgb(6,10,21));
        float sc=Math.min(w/360f,h/290f);
        actual.save();actual.translate((w-360*sc)*.5f,(h-290*sc)*.5f);actual.scale(sc,sc);
        Canvas c=actual;
        long now=SystemClock.uptimeMillis();
        if(!permanent&&untilMs>0&&now>=untilMs){
            mood="neutral";blush=false;untilMs=0;
        }
        boolean sleep="sleep".equals(mood),joy="happy".equals(mood),
            love="love".equals(mood),shy="shy".equals(mood),
            sad="sad".equals(mood),shock="surprised".equals(mood),
            angry="angry".equals(mood);
        int accent=(love||shy)?0xffff83c4:angry?0xffff8c8c:sad?0xff8db7ff:
            sleep?0xff8ba3bf:0xff6ee8f9;
        clr(0xff131d38);
        p.setShader(new RadialGradient(180,139,188,new int[]{0xff233b57,0xff0b1728,0xff070b15},null,Shader.TileMode.CLAMP));
        c.drawCircle(180,141,180,p);p.setShader(null);
        float breathe=(float)Math.sin(now/600.0)*2.4f;
        boolean blink=now%4400<120||sleep;
        float eyeH=blink?7:shock?63:(joy||shy)?25:angry?30:49;
        float y=102+breathe;
        clr(accent);p.setShadowLayer(15,0,0,accent);
        c.drawRoundRect(new RectF(70,y,147,y+eyeH),Math.min(24,eyeH/2),Math.min(24,eyeH/2),p);
        c.drawRoundRect(new RectF(213,y,290,y+eyeH),Math.min(24,eyeH/2),Math.min(24,eyeH/2),p);
        p.clearShadowLayer();
        if(angry){
            line(c,70,y-12,147,y+4,accent,5);line(c,213,y+4,290,y-12,accent,5);
        }
        if(sad){
            line(c,70,y-8,107,y-20,accent,4);line(c,253,y-20,290,y-8,accent,4);
        }
        if(blush){
            clr(0x66ff7fb5);c.drawOval(new RectF(35,163,109,197),p);
            c.drawOval(new RectF(251,163,325,197),p);
            clr(0xaaff90c9);
            for(int i=0;i<3;i++){
                c.drawCircle(53+i*13,181+(i%2)*4,2,p);
                c.drawCircle(274+i*13,181+(i%2)*4,2,p);
            }
        }
        if(shock){
            clr(accent);c.drawOval(new RectF(164,192,196,234),p);
        }else{
            clr(accent);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(7);p.setStrokeCap(Paint.Cap.ROUND);
            Path mouth=new Path();mouth.moveTo(143,207);
            mouth.quadTo(180,joy||love?247:sad?188:224,217,207);
            c.drawPath(mouth,p);
        }
        if(love){
            text(c,"♥",310,96,35,0xffff92ca);text(c,"♥",49,88,24,0xffffa5d9);
        }
        if(sleep){text(c,"Z z z",300,82,19,0xff8bb6d5);}
        String caption=love?"Zakochana":joy?"Radosna":shy?"Nieśmiała":
            sad?"Smutna":shock?"Zdziwiona":angry?"Zła":sleep?"Śpi":"Neutralna";
        text(c,"A L A",180,35,18,0xffbdcee4);
        text(c,caption,180,267,14,0xffd7eaf8);
        if(scheduledMs>now){
            rounded(c,76,40,284,72,13,0xdd20364f);
            text(c,"Za "+Math.max(1,(scheduledMs-now+999)/1000)+" s",180,61,17,0xffffffff);
        }
        actual.restore();
        if(getWindowVisibility()==VISIBLE&&isAttachedToWindow())postInvalidateDelayed(85);
    }
}
