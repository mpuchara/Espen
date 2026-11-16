package pl.espen.garett.facecheck;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

/** Zero-network, zero-permissions, single-DEX installation diagnostic for Garett Android 8.1. */
public class FaceCheckActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
        setContentView(new SimpleFace());
    }
    final class SimpleFace extends View {
        private final Paint p=new Paint(3);
        private long nextBlink=SystemClock.uptimeMillis()+1600;
        private long blinkUntil=0, happyUntil=0, pressedAt=0;
        private int clicks=0;
        SimpleFace(){super(FaceCheckActivity.this);setLayerType(View.LAYER_TYPE_SOFTWARE,null);}
        void fill(int color){p.setColor(color);p.setStyle(Paint.Style.FILL);}
        void rounded(Canvas c,float x,float y,float w,float h,float r,int color){
            fill(color);c.drawRoundRect(new RectF(x,y,x+w,y+h),r,r,p);
        }
        @Override protected void onDraw(Canvas surface) {
            int w=getWidth(),h=getHeight();if(w==0||h==0)return;
            float s=Math.min(w/360f,h/430f);
            surface.drawColor(Color.BLACK);
            surface.save();surface.translate((w-360*s)/2,(h-430*s)/2);surface.scale(s,s);
            Canvas c=surface;
            long now=SystemClock.uptimeMillis();
            if(now>nextBlink){blinkUntil=now+140;nextBlink=now+2300;}
            boolean blink=now<blinkUntil;
            boolean smiling=now<happyUntil;
            int blue=Color.rgb(93,234,255);
            fill(Color.rgb(8,19,27));c.drawCircle(180,195,160,p);
            fill(Color.WHITE);p.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            p.setTextAlign(Paint.Align.CENTER);p.setTextSize(20);
            c.drawText("A L A  ·  TEST 8.1",180,48,p);
            int eyeHeight=blink?9:smiling?29:57;
            rounded(c,78,125,85,eyeHeight,eyeHeight/2f,blue);
            rounded(c,197,125,85,eyeHeight,eyeHeight/2f,blue);
            p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(8);p.setColor(blue);
            p.setStrokeCap(Paint.Cap.ROUND);
            Path mouth=new Path();mouth.moveTo(136,260);
            mouth.quadTo(180,smiling?312:287,224,260);c.drawPath(mouth,p);
            fill(Color.rgb(177,220,236));p.setTextSize(17);
            c.drawText(smiling?"Super! Głaskanie działa!":"Dotknij buźkę 👆",180,351,p);
            p.setTextSize(14);c.drawText("Kliknięcia: "+clicks,180,385,p);
            p.setTextSize(12);c.drawText("Test instalacji · bez Internetu i mikrofonu",180,410,p);
            surface.restore();
            postInvalidateDelayed(110);
        }
        @Override public boolean onTouchEvent(MotionEvent e){
            if(e.getAction()==MotionEvent.ACTION_DOWN){pressedAt=SystemClock.uptimeMillis();return true;}
            if(e.getAction()==MotionEvent.ACTION_UP){
                clicks++;happyUntil=SystemClock.uptimeMillis()+2300;invalidate();performClick();return true;
            }
            return true;
        }
        @Override public boolean performClick(){super.performClick();return true;}
    }
}
