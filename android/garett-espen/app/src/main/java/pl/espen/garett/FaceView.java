package pl.espen.garett;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import java.util.Locale;
import java.util.Random;

/** Watch-friendly vector face: inspired by Espen's expressive face engine, not LVGL code. */
public final class FaceView extends View {
    public enum State { IDLE, CONNECTING, LISTENING, THINKING, SPEAKING, ERROR }
    public interface Actions { void onMicrophone(); void onCamera(); void onSettings(); }
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Random rng = new Random();
    private Actions actions;
    private State state = State.IDLE;
    private String emotion = "neutral", subtitle = "", status = "Dotknij mikrofonu";
    private String activationCode = "";
    private int micLevel = 0, speakerLevel = 0;
    private long moodUntil = 0, nextBlink = 0, blinkUntil = 0, nextLook = 0;
    private long heartUntil = 0, downAt = 0, lastTap = 0;
    private float downX, downY, movement, lookX = 0, lookY = 0;
    private boolean touching, isActive;
    public FaceView(Context c) {
        super(c); setLayerType(View.LAYER_TYPE_SOFTWARE, null); setClickable(true);
        nextBlink = SystemClock.uptimeMillis() + 2200;
    }
    public void setActions(Actions a) { actions = a; }
    public void setState(State s) { state = s; if(s==State.IDLE || s==State.CONNECTING) subtitle=""; invalidate(); }
    public State getState() { return state; }
    public void setStatus(String s) { status = s == null ? "" : s; invalidate(); }
    public void setSubtitle(String s) {
        subtitle = s == null ? "" : s;
        if (subtitle.length() > 90) subtitle = subtitle.substring(0, 90) + "…";
        invalidate();
    }
    public void setActivationCode(String c) { activationCode = c == null ? "" : c; invalidate(); }
    public void setEmotion(String s) {
        emotion = s == null ? "neutral" : s.toLowerCase(Locale.ROOT);
        moodUntil = SystemClock.uptimeMillis() + 3700;
        if (emotion.equals("loving") || emotion.equals("love")) heartUntil = moodUntil;
        invalidate();
    }
    public void setMicLevel(int v) { micLevel = Math.max(0,Math.min(100,v)); }
    public void setSpeakerLevel(int v) { speakerLevel = Math.max(0,Math.min(100,v)); }
    private void color(int c) { p.reset(); p.setAntiAlias(true); p.setColor(c); }
    private void round(Canvas c,float x,float y,float w,float h,float r,int clr) {
        color(clr); c.drawRoundRect(new RectF(x,y,x+w,y+h),r,r,p);
    }
    private void text(Canvas c,String s,float x,float y,float size,int clr,boolean bold) {
        color(clr); p.setTextSize(size); p.setTypeface(bold ? android.graphics.Typeface.create("sans-serif",1) : android.graphics.Typeface.create("sans-serif",0));
        p.setTextAlign(Paint.Align.CENTER); c.drawText(s,x,y,p);
    }
    private void line(Canvas c,float x1,float y1,float x2,float y2,int clr,float width) {
        color(clr); p.setStyle(Paint.Style.STROKE);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeWidth(width);
        c.drawLine(x1,y1,x2,y2,p);
    }
    private int accent() {
        String e = emotion;
        if (state == State.ERROR) return Color.rgb(255,123,122);
        if (SystemClock.uptimeMillis() < moodUntil) {
            if (e.contains("sad")||e.contains("cry")) return Color.rgb(130,176,255);
            if (e.contains("angry")) return Color.rgb(255,144,122);
            if (e.contains("love")||e.contains("loving")||e.contains("shy")) return Color.rgb(255,145,199);
            if (e.contains("happy")||e.contains("laugh")||e.contains("excited")) return Color.rgb(124,246,218);
            if (e.contains("sleep")) return Color.rgb(121,170,191);
        }
        return Color.rgb(104,228,255);
    }
    @Override protected void onDraw(Canvas real) {
        super.onDraw(real);
        int w=getWidth(),h=getHeight(); if (w<=0||h<=0)return;
        real.drawColor(Color.BLACK);
        float scale=Math.min(w/360f,h/400f);
        real.save(); real.translate((w-360*scale)/2f,(h-400*scale)/2f);real.scale(scale,scale);
        Canvas c=real;
        long now=SystemClock.uptimeMillis();
        if(now>=nextBlink){blinkUntil=now+140;nextBlink=now+1800+rng.nextInt(3100);}
        if(now>=nextLook){lookX=rng.nextInt(19)-9;lookY=rng.nextInt(11)-5;nextLook=now+1700+rng.nextInt(1200);}
        float driftX=(float)Math.sin(now/1650.0)*2 + lookX*.5f;
        float driftY=(float)Math.sin(now/2050.0)*3 + lookY*.5f;
        int a=accent();
        color(0xff081721);p.setShader(new RadialGradient(180,174,193,new int[]{0xff142d39,0xff06131d,Color.BLACK},null,Shader.TileMode.CLAMP));
        c.drawCircle(180,172,193,p);p.setShader(null);
        text(c,"A  L  A",180,32,15,0xff91a7b4,true);
        int left=state==State.LISTENING?-35:0;
        float bob=(float)Math.sin(now/490.0)*2f;
        boolean sleepy=(now>=moodUntil && state==State.IDLE && now>downAt+25000);
        boolean blink=now<blinkUntil || sleepy;
        boolean surprised=now<moodUntil&&(emotion.contains("surpris")||emotion.contains("shock"));
        boolean laughing=now<moodUntil&&(emotion.contains("laugh")||emotion.contains("happy"));
        boolean sad=now<moodUntil&&(emotion.contains("sad")||emotion.contains("cry"));
        boolean angry=now<moodUntil&&emotion.contains("angry");
        boolean wink=now<moodUntil&&emotion.contains("wink");
        float eyeH=blink?9:surprised?71:laughing?21:sad?30:angry?29:49;
        float eyeW=surprised?60:76;
        float ey=122+driftY+bob, lx=91+driftX+left,rx=193+driftX+left;
        color(a);p.setShadowLayer(19,0,0,a);
        c.drawRoundRect(new RectF(lx,ey,lx+eyeW,ey+eyeH),Math.min(24,eyeH/2),Math.min(24,eyeH/2),p);
        if (!wink) c.drawRoundRect(new RectF(rx,ey,rx+eyeW,ey+eyeH),Math.min(24,eyeH/2),Math.min(24,eyeH/2),p);
        else {line(c,rx,ey+25,rx+eyeW,ey+22,a,8);}
        p.clearShadowLayer();
        if(angry){
            line(c,lx,ey-11,lx+eyeW,ey+2,a,6);
            line(c,rx,ey+2,rx+eyeW,ey-11,a,6);
        }
        if (sad){line(c,lx,ey-11,lx+34,ey-20,a,5);line(c,rx+42,ey-20,rx+eyeW,ey-11,a,5);}
        if (state==State.THINKING) {
            for(int i=0;i<3;i++){color(a);c.drawCircle(255+i*13,223-(i%2)*9,3+i,p);}
        }
        float mouthX=180+driftX+left, mouthY=233+bob;
        int intensity=state==State.SPEAKING?speakerLevel:0;
        if(state==State.SPEAKING && intensity>7) {
            float mh=10+intensity*.53f;
            color(a); p.setShadowLayer(8,0,0,a);
            c.drawRoundRect(new RectF(mouthX-37,mouthY-3,mouthX+37,mouthY+mh),Math.min(21,mh/2),Math.min(21,mh/2),p);
            p.clearShadowLayer();
        } else if(surprised) {
            color(a);c.drawOval(new RectF(mouthX-17,mouthY-3,mouthX+17,mouthY+37),p);
        } else {
            color(a);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(7);p.setStrokeCap(Paint.Cap.ROUND);
            Path mouth=new Path();mouth.moveTo(mouthX-37,mouthY);
            mouth.quadTo(mouthX,mouthY+(laughing?33:sad?-17:17),mouthX+37,mouthY);
            c.drawPath(mouth,p);
        }
        if (now<heartUntil) {
            for(int i=0;i<5;i++){
                float age=((now+i*170)%1750)/1750f;
                float hx=55+i*62+(float)Math.sin(now/340f+i)*9;
                float hy=260-age*150;
                color(0xffff91c7);p.setAlpha((int)(185*(1-age)));
                Path heart=new Path();heart.moveTo(hx,hy+10);heart.cubicTo(hx-19,hy-2,hx-7,hy-16,hx,hy-5);
                heart.cubicTo(hx+7,hy-16,hx+19,hy-2,hx,hy+10);c.drawPath(heart,p);
            }
        }
        if(state==State.LISTENING) {
            float power=micLevel/100f;
            for(int i=0;i<4;i++){
                int hh=(int)(12+(20+i*6)*power+Math.sin(now/160.0+i)*4);
                round(c,276+i*13,173-hh/2f,6,hh,3,a);
            }
        }
        if(activationCode.length()>0){
            round(c,18,278,324,51,13,0xff173041);
            text(c,"KOD XIAOZHI: "+activationCode,180,310,19,0xffd8efff,true);
        } else {
            String visible=state==State.SPEAKING && subtitle.length()>0?subtitle:status;
            if(visible.length()>40) visible=visible.substring(0,39)+"…";
            text(c,visible,180,302,13,0xffbed3dd,false);
        }
        // Bottom touch targets: settings, mic, camera; icons remain visible on tiny screens.
        round(c,26,327,65,52,18,0xff172a38);
        text(c,"⚙",58,362,30,0xffc1e7fb,false);
        int micColor=(state==State.LISTENING||state==State.CONNECTING)?0xff58e9ce:0xff233f4f;
        round(c,121,321,118,62,25,micColor);
        color(state==State.LISTENING||state==State.CONNECTING?0xff00353a:0xffb5efff);
        c.drawRoundRect(new RectF(172,335,188,357),8,8,p);
        line(c,164,350,164,361,p.getColor(),3);line(c,196,350,196,361,p.getColor(),3);line(c,164,361,196,361,p.getColor(),3);line(c,180,361,180,368,p.getColor(),3);
        round(c,267,327,67,52,18,0xff172a38);
        color(0xffb5efff);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(3);
        c.drawRoundRect(new RectF(283,342,318,364),4,4,p);c.drawCircle(301,353,7,p);
        text(c,"",180,393,8,0xff718b9c,false);
        real.restore();
        if(getWindowVisibility()==VISIBLE && isAttachedToWindow()) postInvalidateDelayed(state==State.IDLE?180:90);
    }
    private float logicalX(MotionEvent e){
        float s=Math.min(getWidth()/360f,getHeight()/400f);
        return (e.getX()-(getWidth()-360*s)/2)/s;
    }
    private float logicalY(MotionEvent e){
        float s=Math.min(getWidth()/360f,getHeight()/400f);
        return (e.getY()-(getHeight()-400*s)/2)/s;
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        float x=logicalX(e),y=logicalY(e);long now=SystemClock.uptimeMillis();
        switch(e.getActionMasked()){
            case MotionEvent.ACTION_DOWN:
                downX=x;downY=y;downAt=now;movement=0;touching=true;isActive=true;return true;
            case MotionEvent.ACTION_MOVE:
                if(touching){movement=Math.max(movement,Math.abs(x-downX)+Math.abs(y-downY));if(movement>25&&downY<318){emotion="love";moodUntil=now+1700;heartUntil=now+1700;}}
                return true;
            case MotionEvent.ACTION_CANCEL: touching=false;return true;
            case MotionEvent.ACTION_UP:
                touching=false;isActive=false;
                if(now-downAt>=1500 && movement<20){if(actions!=null)actions.onSettings();return true;}
                if(y>314&&movement<35){
                    if(actions!=null){if(x<105)actions.onSettings();else if(x>251)actions.onCamera();else actions.onMicrophone();}
                }else if(movement<25){
                    if(now-lastTap<420){emotion="surprised";moodUntil=now+1600;}
                    else{emotion="winking";moodUntil=now+950;}
                    lastTap=now;
                }
                invalidate();return true;
        }
        return true;
    }
}
