package pl.espen.garett;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Shader;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import java.util.Calendar;
import android.graphics.Typeface;
import java.util.Random;

/** Watch-friendly vector face: inspired by Espen's expressive face engine, not LVGL code. */
public final class FaceView extends View {
    public interface Actions {
        void onMicrophone(); void onCamera(); void onGames(); void onChoice(int index); void onSettings();
    }
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Random rng=new Random();
    private final AlaBehavior behavior=new AlaBehavior(SystemClock.uptimeMillis(),rng);
    private final Path path=new Path();
    private final ThinkingNetwork network=new ThinkingNetwork();
    private final AlaSocial.PetMotion pet=new AlaSocial.PetMotion();
    private final RadialGradient background=new RadialGradient(180,174,193,
        new int[]{0xff142d39,0xff06131d,Color.BLACK},null,Shader.TileMode.CLAMP);
    private final boolean watch;
    private final VisualFrames visualFrames;
    private int sceneFollow;
    private Actions actions;
    private AlaBehavior.Phase state=AlaBehavior.Phase.IDLE;
    private AlaBehavior.Mood previous=AlaBehavior.Mood.NEUTRAL,target=previous;
    private String subtitle="",status="Dotknij mikrofonu",activationCode="",activationLabel="",choiceTitle="";
    private String[] choices;
    private String[] titleLines={""};
    private int highlightedChoice=-1,micLevel,speakerLevel,hour;
    private long nextBlink,blinkAt=-1000,downAt,lastTap=-1000,transitionAt,refreshHour;
    private int touchCount;
    private float downX,downY,movement,lookX,lookY;
    private boolean touching,foreground=true,game,story;
    private boolean micEnabled,choicesEnabled,dockEnabled,statusError;
    private long shutterAt=-1000,storyAt;
    private int pressedDock=-1,pressedChoice=-1;
    private NarrationCue.Scene scene;

    public FaceView(Context context,boolean watch){
        super(context);
        this.watch=watch;
        visualFrames=VisualFrames.load(context);
        setClickable(true);
        nextBlink=SystemClock.uptimeMillis()+2200;
    }

    public void setActions(Actions actions){this.actions=actions;}
    public void setState(AlaBehavior.Phase next){
        state=next;
        long now=SystemClock.uptimeMillis();
        behavior.phase(next,now);
        updateThinking(now);
        if (next!=AlaBehavior.Phase.SPEAKING) {
            scene=null;
            sceneFollow=0;
        }
        if (next!=AlaBehavior.Phase.SPEAKING) speakerLevel=0;
        if (next!=AlaBehavior.Phase.LISTENING) micLevel=0;
        if (next==AlaBehavior.Phase.IDLE || next==AlaBehavior.Phase.CONNECTING) subtitle="";
        invalidate();
    }
    void foreground(boolean visible){
        foreground=visible;
        if (visible) behavior.interact(SystemClock.uptimeMillis());
        else {
            setState(AlaBehavior.Phase.IDLE);
            network.clear();
            shutterAt=-1000;
        }
        invalidate();
    }
    void game(boolean active){game=active;}
    void setControls(boolean canMic,boolean canChoose,boolean canDock){
        micEnabled=canMic;
        choicesEnabled=canChoose;
        dockEnabled=canDock;
        invalidate();
    }
    void setStory(boolean active){
        if (story!=active) {
            story=active;
            updateThinking(SystemClock.uptimeMillis());
            invalidate();
        }
    }
    void storySentence(String text){
        long now=SystemClock.uptimeMillis();
        behavior.reply(NarrationCue.mood(text),now);
        NarrationCue.Scene next=NarrationCue.scene(text);
        if (next==null && scene!=null && sceneFollow++<2) return;
        if (next==null) next=NarrationCue.Scene.Talk;
        else sceneFollow=0;
        if (next!=scene) {
            scene=next;
            storyAt=now;
        }
    }
    void shutter(){shutterAt=SystemClock.uptimeMillis();invalidate();}
    private void updateThinking(long now){
        ThinkingNetwork.Mode mode=ThinkingNetwork.Mode.OFF;
        if (state==AlaBehavior.Phase.VISION_ANALYZING) mode=ThinkingNetwork.Mode.VISION;
        else if (state==AlaBehavior.Phase.THINKING)
            mode=story?ThinkingNetwork.Mode.STORY:ThinkingNetwork.Mode.REPLY;
        network.mode(mode,now);
    }
    void userSpeech(String text){behavior.speech(text,SystemClock.uptimeMillis());invalidate();}
    void reaction(AlaBehavior.Mood mood){behavior.react(mood,SystemClock.uptimeMillis());invalidate();}
    boolean errorVisible(){return statusError || state==AlaBehavior.Phase.ERROR;}
    void setStatus(String message,boolean error){
        status=watchLabel(message,40);
        statusError=error;
        invalidate();
    }
    public void setSubtitle(String message){
        subtitle=watchLabel(message,40);
        invalidate();
    }
    public void setActivationCode(String code){
        activationCode=code==null?"":code;
        activationLabel="KOD XIAOZHI: "+activationCode;
        invalidate();
    }
    private static String watchLabel(String value,int limit){
        if (value==null) return "";
        if (value.length()<=limit) return value;
        int end=limit-1;
        if (Character.isHighSurrogate(value.charAt(end-1))) end--;
        return value.substring(0,end)+"…";
    }
    public void setChoices(String title, String[] buttons) {
        choiceTitle=title==null?"":title;
        int split=choiceTitle.length()<=42 ? -1 : choiceTitle.lastIndexOf(' ',42);
        titleLines=split<=0 ? new String[]{choiceTitle}
            : new String[]{choiceTitle.substring(0,split),choiceTitle.substring(split+1)};
        choices=watch && buttons!=null?buttons.clone():null;
        if (choices!=null) for (int i=0;i<choices.length;i++) choices[i]=watchLabel(choices[i],32);
        highlightedChoice=-1;
        invalidate();
    }
    public void setHighlightedChoice(int index) { highlightedChoice=index; invalidate(); }
    public void setEmotion(String emotion){
        behavior.cloud(emotion,SystemClock.uptimeMillis());
        invalidate();
    }
    public void setMicLevel(int v) { micLevel = Math.max(0,Math.min(100,v)); }
    public void setSpeakerLevel(int v) { speakerLevel = Math.max(0,Math.min(100,v)); }
    private void color(int c) { p.reset(); p.setAntiAlias(true); p.setColor(c); }
    private void round(Canvas c,float x,float y,float w,float h,float r,int clr) {
        color(clr); c.drawRoundRect(x,y,x+w,y+h,r,r,p);
    }
    private void text(Canvas c,String s,float x,float y,float size,int clr,boolean bold) {
        color(clr); p.setTextSize(size); p.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        p.setTextAlign(Paint.Align.CENTER); c.drawText(s,x,y,p);
    }
    private void line(Canvas c,float x1,float y1,float x2,float y2,int clr,float width) {
        color(clr); p.setStyle(Paint.Style.STROKE);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeWidth(width);
        c.drawLine(x1,y1,x2,y2,p);
    }
    private static float ease(float progress){return progress*progress*(3-2*progress);}
    private static float blend(float from,float to,float amount){return from+(to-from)*amount;}

    @Override protected void onDraw(Canvas real) {
        super.onDraw(real);
        int width=getWidth(),height=getHeight();
        if (width<=0 || height<=0) return;
        real.drawColor(watch?Color.BLACK:0xff09141d);
        float scale=scale();
        real.save();
        real.translate((width-360*scale)/2f,(height-logicalHeight()*scale)/2f);
        real.scale(scale,scale);
        Canvas c=real;
        long now=SystemClock.uptimeMillis();
        if (now>=refreshHour) {
            hour=Calendar.getInstance().get(Calendar.HOUR_OF_DAY);
            refreshHour=now+60_000;
        }
        AlaBehavior.Mood mood=behavior.mood(now,hour,game);
        if (mood!=target) {
            previous=target;
            target=mood;
            transitionAt=now;
        }
        float transition=ease(Math.min(1,(now-transitionAt)/650f));
        AlaBehavior.Cue cue=behavior.idleCue(now,hour,game);
        if (now>=nextBlink) {
            blinkAt=now;
            nextBlink=now+(mood==AlaBehavior.Mood.SLEEPY?4500:2600)+rng.nextInt(3100);
        }
        float gazeX=cue==AlaBehavior.Cue.LEFT?-19:cue==AlaBehavior.Cue.RIGHT?19:0;
        float gazeY=cue==AlaBehavior.Cue.UP?-11:cue==AlaBehavior.Cue.DOWN?10:0;
        if (state==AlaBehavior.Phase.LISTENING) gazeX=-16;
        if (network.visible(now)) {
            gazeX+=(float)Math.sin(now/580.0)*5f;
            gazeY-=(float)(2+2*Math.sin(now/470.0));
        }
        if (state==AlaBehavior.Phase.SPEAKING && story && scene!=null) {
            gazeX+=(now-storyAt)%2600<1500?10:0;
            gazeY+=(float)Math.sin(now/310.0)*2;
        }
        lookX=blend(lookX,gazeX,.15f);
        lookY=blend(lookY,gazeY,.15f);
        float blink=now-blinkAt<180 ? (float)Math.sin((now-blinkAt)*Math.PI/180) : 0;
        if (behavior.cueBlink(now,cue)) blink=Math.max(blink,.88f);
        float eyeH=blend(previous.eyeHeight,target.eyeHeight,transition);
        if (cue==AlaBehavior.Cue.SQUINT) eyeH=Math.max(9,eyeH-16);
        eyeH=blend(eyeH,5,blink)*.82f;
        float asym=blend(previous.asymmetry,target.asymmetry,transition);
        if (mood==AlaBehavior.Mood.WINK && (now-transitionAt)%1400<420) asym+=27;
        if (mood==AlaBehavior.Mood.CONFUSED) asym+=(float)Math.sin(now/180.0)*9;
        float eyeW=blend(previous.eyeWidth,target.eyeWidth,transition);
        float eyeY=144+lookY+(float)Math.sin(now/800.0)*2;
        int a=state==AlaBehavior.Phase.ERROR ? 0xffff907b : target.color;
        if (watch) {
            color(Color.BLACK);
            p.setShader(background);
            c.drawCircle(180,172,193,p);
        }
        text(c,"A  L  A",180,watch?22:31,15,0xffa8c2cf,true);
        c.save();
        if (watch && choices!=null) c.translate(0,-21);
        if (story) drawStorySparkles(c,now);
        if (state==AlaBehavior.Phase.SPEAKING && story && scene!=null) {
            if (visualFrames!=null) {
                if (target==AlaBehavior.Mood.NEUTRAL) a=mixColor(a,visualFrames.tint(scene));
                visualFrames.draw(c,p,scene,now-storyAt,watch);
            }
        }
        network.draw(c,now,watch);
        c.save();
        if (!watch) c.scale(1.16f,1.08f,180,145);
        float tilt=blend(previous.tilt,target.tilt,transition);
        if (cue==AlaBehavior.Cue.TILT) tilt+=7;
        if (mood==AlaBehavior.Mood.LAUGH || mood==AlaBehavior.Mood.EXCITED)
            tilt+=(float)Math.sin(now/145.0)*5;
        c.rotate(tilt,180,eyeY);
        drawEye(c,128+lookX,eyeY,eyeW,eyeH,a);
        drawEye(c,232+lookX,eyeY,eyeW,Math.max(5,eyeH-asym*.65f),a);
        if (target.brow>=14 || target==AlaBehavior.Mood.SURPRISE || target==AlaBehavior.Mood.FEAR) {
            float slope=target==AlaBehavior.Mood.ANGRY?10:-8;
            line(c,105+lookX,eyeY-eyeH/2-12,150+lookX,eyeY-eyeH/2-12+slope,a,4);
            line(c,210+lookX,eyeY-eyeH/2-12+slope,255+lookX,eyeY-eyeH/2-12,a,4);
        }
        c.restore();
        float mouthX=180+lookX,mouthY=207+lookY;
        float mouthW=blend(previous.mouthWidth,target.mouthWidth,transition)*.36f;
        float mouthH=blend(previous.mouthHeight,target.mouthHeight,transition);
        int intensity=state==AlaBehavior.Phase.SPEAKING?speakerLevel:0;
        if (intensity>4 || mouthH>22) {
            float opening=intensity>4 ? 7+intensity*.40f : mouthH*.65f;
            if (intensity>56 && (now-storyAt)%240<125) mouthW*=.66f;
            if (intensity>76 && (now-storyAt)%240>=125) mouthW*=1.17f;
            round(c,mouthX-mouthW,mouthY-opening/2,2*mouthW,opening,opening*.42f,a);
        } else {
            float smile=blend(previous.smile,target.smile,transition)+(cue==AlaBehavior.Cue.SMILE?6:0);
            color(a);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(6);p.setStrokeCap(Paint.Cap.ROUND);
            path.rewind();path.moveTo(mouthX-mouthW,mouthY);
            path.quadTo(mouthX,mouthY+smile,mouthX+mouthW,mouthY);
            c.drawPath(path,p);
        }
        drawAccent(c,mood.accent,now,a);
        c.restore();
        if(state==AlaBehavior.Phase.LISTENING) {
            float power=micLevel/100f;
            for(int i=0;i<4;i++){
                int hh=(int)(12+(20+i*6)*power+Math.sin(now/160.0+i)*4);
                round(c,276+i*13,173-hh/2f,6,hh,3,a);
            }
        }
        if (watch) drawWatchControls(c,now);
        if (now-shutterAt<290) {
            color(Color.argb((int)(107*(1-(now-shutterAt)/290f)),211,248,255));
            c.drawRoundRect(9,7,351,watch?263:logicalHeight()-7,22,22,p);
        }
        real.restore();
        if (foreground && getWindowVisibility()==VISIBLE && isAttachedToWindow()) {
            boolean moving=blink>0 || transition<1 || cue!=AlaBehavior.Cue.NONE ||
                (state!=AlaBehavior.Phase.IDLE && state!=AlaBehavior.Phase.ERROR) || highlightedChoice>=0
                || network.visible(now) || now-shutterAt<290;
            postInvalidateDelayed(moving ? (watch?32:16) : 100);
        }
    }
    private int dockColor(boolean enabled,int index){
        return enabled?(pressedDock==index?0xff346878:0xff173043):0xff111e28;
    }

    private void drawWatchControls(Canvas c,long now){
        // Menus occupy the lower face area; eyes stay visible during gameplay.
        // Narrative text-to-speech clears the menu so the complete face can animate.
        if(choices!=null && choices.length>0){
            round(c,13,196,334,125,13,0xff102736);
            drawTitle(c);
            for(int i=0;i<Math.min(3,choices.length);i++){
                boolean active=(highlightedChoice==i);
                float glow=active?(float)(0.72+0.28*Math.sin(now/165.0)):0f;
                round(c,22,223+i*32,316,30,9,active?Color.argb((int)(157+98*glow),67,143,148):
                    choicesEnabled?(pressedChoice==i?0xff367585:0xff244452):0xff172936);

                text(c,choices[i],180,243+i*32,13,active?0xffefffff:choicesEnabled?0xffc6e4ed:0xff8094a0,false);
            }
        }else if(activationCode.length()>0 && !statusError && state!=AlaBehavior.Phase.ERROR){
            round(c,18,278,324,51,13,0xff173041);
            text(c,activationLabel,180,310,19,0xffd8efff,true);
        } else {
            String visible=state==AlaBehavior.Phase.SPEAKING && subtitle.length()>0?subtitle:status;
            text(c,visible,180,302,13,statusError || state==AlaBehavior.Phase.ERROR?0xffffa18e:0xffbed3dd,false);
        }
        c.save();

        // Three main actions from ESP32-P4 0.2.28: Games, Camera, Microphone.
        round(c,17,326,101,57,18,dockColor(dockEnabled,0));
        round(c,130,326,100,57,18,dockColor(dockEnabled,1));
        int micColor=micEnabled && state==AlaBehavior.Phase.LISTENING?0xff58e9ce:dockColor(micEnabled,2);
        round(c,242,326,101,57,18,micColor);
        // Gamepad, without relying on installed emoji fonts.
        round(c,44,341,49,25,11,dockEnabled?0xff8be6f3:0xff64808c);
        line(c,54,353,67,353,0xff153743,3);line(c,60,347,60,359,0xff153743,3);
        color(0xff153743);c.drawCircle(78,349,3,p);c.drawCircle(83,357,3,p);
        // Camera glyph.
        color(dockEnabled?0xffb5efff:0xff64808c);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(3);
        c.drawRoundRect(153,343,207,366,5,5,p);
        c.drawCircle(180,354,8,p);
        line(c,161,341,175,341,dockEnabled?0xffb5efff:0xff64808c,3);
        // Microphone glyph and optional active state.
        color(!micEnabled?0xff64808c:state==AlaBehavior.Phase.LISTENING?0xff00353a:0xffb5efff);
        int micInk=p.getColor();
        c.drawRoundRect(285,339,299,359,7,7,p);
        line(c,278,352,278,362,micInk,3);
        line(c,306,352,306,362,micInk,3);
        line(c,278,362,306,362,micInk,3);
        line(c,292,362,292,371,micInk,3);
        c.restore();
        // Settings remain accessible by long press and top-right gear.
        text(c,"⚙",325,36,20,0xff8ba7b8,false);
    }

    private void drawStorySparkles(Canvas c,long now){
        for (int i=0;i<4;i++) {
            float wave=(float)(.5+.5*Math.sin(now/570.0+i*1.57));
            float x=i%2==0?38+i*4:323-i*4;
            float y=i<2?43+wave*3:220+wave*3;
            int ink=Color.argb((int)(70+150*wave),145,208,246);
            line(c,x-4,y,x+4,y,ink,1.3f);
            line(c,x,y-4,x,y+4,ink,1.3f);
        }
    }

    private static int mixColor(int first,int second){
        return Color.rgb((Color.red(first)+Color.red(second))/2,
            (Color.green(first)+Color.green(second))/2,(Color.blue(first)+Color.blue(second))/2);
    }

    private void drawEye(Canvas c,float x,float y,float width,float height,int color){
        round(c,x-width/2-3,y-height/2-3,width+6,height+6,18,(color&0x00ffffff)|0x41000000);
        round(c,x-width/2,y-height/2,width,height,Math.min(25,height/2),color);
    }

    private void drawTitle(Canvas c){
        if (titleLines.length==1) text(c,titleLines[0],180,216,13,0xffddfaff,true);
        else {
            text(c,titleLines[0],180,208,12,0xffddfaff,true);
            text(c,titleLines[1],180,220,12,0xffddfaff,true);
        }
    }

    private void drawAccent(Canvas c,AlaBehavior.Accent accent,long now,int ink){
        switch (accent) {
            case HEART:
                for (int i=0;i<4;i++) {
                    float age=((now+i*330)%1750)/1750f;
                    float x=57+i*74,y=218-age*111;
                    color(ink);p.setAlpha((int)(211*(1-age)));
                    path.rewind();path.moveTo(x,y+10);
                    path.cubicTo(x-19,y-2,x-7,y-16,x,y-5);
                    path.cubicTo(x+7,y-16,x+19,y-2,x,y+10);
                    c.drawPath(path,p);
                }
                break;
            case STAR:
                text(c,"✦",68,105+(float)Math.sin(now/150.0)*5,31,ink,true);
                text(c,"✦",291,115-(float)Math.sin(now/150.0)*5,23,ink,true);
                break;
            case TEAR:
                color(ink);c.drawOval(237,167+now%800/60f,247,189+now%800/60f,p);
                break;
            case BLUSH:
                round(c,77,176,35,10,5,0x90ff80ab);
                round(c,248,176,35,10,5,0x90ff80ab);
                break;
            case DOTS:
                for (int i=0;i<3;i++) {
                    color(ink);p.setAlpha((int)(150+90*Math.sin(now/230.0+i)));
                    c.drawCircle(266+i*12,190-i*5,3+i,p);
                }
                break;
            case SLEEP:
                text(c,"z",251,125,14,ink,true);
                text(c,"Z",272,108,19,ink,true);
                break;
            default: break;
        }
    }

    private float logicalHeight(){return watch?400:254;}
    private float scale(){return Math.max(.01f,Math.min(getWidth()/360f,getHeight()/logicalHeight()));}
    private float logicalX(MotionEvent e){
        float s=scale();
        return (e.getX()-(getWidth()-360*s)/2)/s;
    }
    private float logicalY(MotionEvent e){
        float s=scale();
        return (e.getY()-(getHeight()-logicalHeight()*s)/2)/s;
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
        float x=logicalX(e),y=logicalY(e);long now=SystemClock.uptimeMillis();
        switch(e.getActionMasked()){
            case MotionEvent.ACTION_DOWN:
                downX=x;downY=y;downAt=now;movement=0;touching=true;
                pressedDock=watch && y>322 && y<393 ? (x<123?0:x<237?1:2) : -1;
                pressedChoice=watch && choices!=null && y>=223 && y<319 ? (int)((y-223)/32f) : -1;
                pet.start(x,y,now);behavior.interact(now);invalidate();return true;
            case MotionEvent.ACTION_MOVE:
                if (touching) {
                    movement=Math.max(movement,Math.abs(x-downX)+Math.abs(y-downY));
                    if (downY>55 && downY<(choices==null?238:190)) {
                        AlaBehavior.Mood mood=pet.move(x,y,now);
                        if (mood!=null) reaction(mood);
                    }
                }
                return true;
            case MotionEvent.ACTION_CANCEL: touching=false;pressedDock=pressedChoice=-1;invalidate();return true;
            case MotionEvent.ACTION_UP:
                touching=false;
                pressedDock=pressedChoice=-1;
                if(now-downAt>=1500 && movement<20){if(actions!=null)actions.onSettings();return true;}
                if(watch && y>322 && y<393 && x>=12 && x<=348 && movement<35){
                    if(actions!=null){if(x<123 && dockEnabled)actions.onGames();else if(x>=123 && x<237 && dockEnabled)actions.onCamera();else if(x>=237 && micEnabled)actions.onMicrophone();}
                }else if(watch && x>293&&y<63&&movement<25){
                    if(actions!=null)actions.onSettings();
                }else if(watch && choices!=null&&y>=223&&y<319&&movement<35){
                    int selected=(int)((y-223)/32f);
                    if(choicesEnabled&&selected>=0&&selected<choices.length&&actions!=null)actions.onChoice(selected);
                }else if(movement<25){
                    touchCount=now-lastTap<420 ? touchCount+1 : 1;
                    if (touchCount>=3) { reaction(AlaBehavior.Mood.EXCITED); touchCount=0; }
                    else if (touchCount==2) reaction(AlaBehavior.Mood.SURPRISE);
                    else reaction(Math.abs(x-180)<24 && y>153 && y<193
                        ? AlaBehavior.Mood.SHY : AlaBehavior.Mood.WINK);
                    lastTap=now;
                }
                invalidate();return true;
        }
        return true;
    }
}
